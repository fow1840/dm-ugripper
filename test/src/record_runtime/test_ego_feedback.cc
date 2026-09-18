#include "record_runtime/ego_feedback.h"
#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

using namespace DA::Ugripper;

static std::string Snapshot(int64_t stamp, int percent = 10) {
    return "timestamp: " + std::to_string(stamp) + "\n"
        "totalCapacity_mAh: 5000\nremainCapacity_mAh: 500\n"
        "totalVoltage_mV: 28900\ncurrent_mA: -123\n"
        "soc_percent: " + std::to_string(percent) + "\n"
        "isCharging: 0\nisDischarging: 1\navgTemperature_degC: 27.3\n";
}

TEST(BmsSnapshotTest, ValidatesFreshnessCompletenessAndValues) {
    auto s = ParseBmsSnapshot(Snapshot(100), 100999);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->percent, 10); EXPECT_EQ(s->capacity_mah, 5000);
    EXPECT_EQ(s->remaining_mah, 500); EXPECT_EQ(s->voltage_mv, 28900);
    EXPECT_EQ(s->current_ma, -123); EXPECT_FALSE(s->charging); EXPECT_TRUE(s->discharging);
    EXPECT_TRUE(ParseBmsSnapshot(Snapshot(100, 0), 120000));
    EXPECT_TRUE(ParseBmsSnapshot(Snapshot(100, 100), 100000));
    EXPECT_FALSE(ParseBmsSnapshot(Snapshot(100), 120001));
    EXPECT_FALSE(ParseBmsSnapshot(Snapshot(101), 100000));
    EXPECT_FALSE(ParseBmsSnapshot(Snapshot(0), 100000));
    EXPECT_FALSE(ParseBmsSnapshot(Snapshot(INT64_MAX), 100000));
    EXPECT_FALSE(ParseBmsSnapshot(Snapshot(100, 101), 100000));
    EXPECT_FALSE(ParseBmsSnapshot(Snapshot(100, -1), 100000));
    EXPECT_FALSE(ParseBmsSnapshot("timestamp: 100\nsoc_percent: 10\n", 100000));
    EXPECT_FALSE(ParseBmsSnapshot(Snapshot(100) + "soc_percent: 9\n", 100000));
    auto bad = Snapshot(100); bad.replace(bad.find("current_mA: -123"), 16, "current_mA: 1.2");
    EXPECT_FALSE(ParseBmsSnapshot(bad, 100000));
    EXPECT_FALSE(ParseBmsSnapshot(std::string(4097, 'x'), 100000));
}

TEST(BmsReaderTest, ReadsSnapshotRejectsStaleAndRecoversWithoutSerial) {
    char temp[] = "/tmp/ugripper-bms-test-XXXXXX";
    ASSERT_NE(mkdtemp(temp), nullptr);
    const auto root = std::filesystem::path(temp);
    const auto source = root / "bms_latest.txt", output = root / "battery.json";
    const auto epoch = [] { return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count(); };
    const auto write = [&](int64_t stamp, int percent) {
        std::ofstream file(source.string() + ".tmp"); file << Snapshot(stamp, percent); file.close();
        std::filesystem::rename(source.string() + ".tmp", source);
    };
    {
        BmsReader reader(source.string(), output.string());
        EXPECT_FALSE(reader.Sample());
        const auto wait = [&](bool connected) {
            const auto deadline = BatterySteadyMs() + 3000;
            while (bool(reader.Sample()) != connected && BatterySteadyMs() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return bool(reader.Sample()) == connected;
        };
        write(epoch(), 5); ASSERT_TRUE(wait(true)); EXPECT_EQ(reader.Sample()->percent, 5);
        write(epoch() - 30, 5); ASSERT_TRUE(wait(false));
        write(epoch(), 0); ASSERT_TRUE(wait(true)); EXPECT_EQ(reader.Sample()->percent, 0);
        std::filesystem::remove(source); ASSERT_TRUE(wait(false));
    }
    nlohmann::json status; std::ifstream(output) >> status;
    EXPECT_FALSE(status.at("connected").get<bool>());
    EXPECT_EQ(status.at("status"), "service_unavailable");
    EXPECT_EQ(status.at("source"), source.string());
    std::filesystem::remove_all(root);
}

TEST(EgoSpeechPolicyTest, WritingDelayIsCancelledAndDoesNotFireForUnmount) {
    EgoSpeechPolicy p;
    p.SetPhase(EgoSpeechPolicy::Phase::Writing,100);
    EXPECT_FALSE(p.Next(4000,{},false));
    p.SetPhase(EgoSpeechPolicy::Phase::Recording,5000);
    p.SetPhase(EgoSpeechPolicy::Phase::Idle,5100);
    p.SetPhase(EgoSpeechPolicy::Phase::Writing,5200);
    EXPECT_FALSE(p.Next(8201,{},false));
    EXPECT_FALSE(p.Next(10200,{},false));
    EXPECT_EQ(p.Next(10201,{},false),1003);
    EXPECT_FALSE(p.Next(11000,{},false));
    p.SetPhase(EgoSpeechPolicy::Phase::Recording,12000);
    p.SetPhase(EgoSpeechPolicy::Phase::Writing,13000);
    p.SetPhase(EgoSpeechPolicy::Phase::Idle,14000);
    EXPECT_FALSE(p.Next(19000,{},false));
}

TEST(EgoSpeechPolicyTest, LowStorageWaitsForRecordingAndRearmsPerEpisode) {
    EgoSpeechPolicy p;
    EXPECT_FALSE(p.Next(5000,{},true));
    p.SetPhase(EgoSpeechPolicy::Phase::Recording,6000);
    EXPECT_FALSE(p.Next(9000,{},true));
    EXPECT_EQ(p.Next(9001,{},true),1203);
    EXPECT_FALSE(p.Next(10000,{},true));
    p.SetPhase(EgoSpeechPolicy::Phase::Idle,11000);
    p.SetPhase(EgoSpeechPolicy::Phase::Recording,12000);
    EXPECT_FALSE(p.Next(16000,{},false));
    EXPECT_EQ(p.Next(17000,{},true),1203);
}

TEST(EgoSpeechPolicyTest, BatteryThresholdsFireOnceAndChargingRearms) {
    EgoSpeechPolicy p; BatterySample b;
    b.percent=11; EXPECT_FALSE(p.Next(10000,b,false));
    for(int value:{10,5,4,3,2,1}) {
        b.percent=value; EXPECT_EQ(p.Next(10000,b,false),1302);
        EXPECT_FALSE(p.Next(11000,b,false));
    }
    b.percent=11; EXPECT_FALSE(p.Next(12000,b,false));
    b.percent=10; EXPECT_FALSE(p.Next(13000,b,false));
    b.charging=true; b.percent=50; EXPECT_FALSE(p.Next(14000,b,false));
    b.charging=false; b.percent=4; EXPECT_EQ(p.Next(15000,b,false),1302);
    EXPECT_FALSE(p.Next(16000,b,false));
    b.percent=3; EXPECT_EQ(p.Next(17000,b,false),1302);
}

TEST(EgoSpeechPolicyTest, ErrorsAreQueuedAndRecordingStartIsProtected) {
    EgoSpeechPolicy p;
    p.SetPhase(EgoSpeechPolicy::Phase::Recording,100);
    p.Notify(1204); p.Notify(1205); p.Notify(1205);
    EXPECT_FALSE(p.Next(3100,{},false));
    EXPECT_EQ(p.Next(3101,{},false),1204);
    EXPECT_EQ(p.Next(3200,{},false),1205);
    EXPECT_FALSE(p.Next(3300,{},false));
}
