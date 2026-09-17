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

static std::array<uint8_t,90> Frame(int percent = 10) {
    std::array<uint8_t,90> f{};
    f[0]=0x68; f[1]=0x5a; f[2]=0xa5; f[89]=0x16;
    f[4]=0xa0; f[5]=0x0f; f[71]=1; f[45]=percent;
    f[41]=0xf4; f[42]=1; f[43]=50;
    for(int i=47;i<51;++i) f[i]=0xff;
    f[66]=0x08;
    for(int i=0;i<88;++i) f[88]^=f[i];
    return f;
}

TEST(BmsParserTest, FragmentedFrameRecoversAfterNoiseAndRejectsBadChecksum) {
    BmsParser p;
    for(auto v:{0x68,0x68,0x12,0x16}) EXPECT_FALSE(p.Feed(v));
    auto bad=Frame(); bad[20]^=1;
    for(auto v:bad) EXPECT_FALSE(p.Feed(v));
    auto good=Frame();
    for(size_t i=0;i<89;++i) EXPECT_FALSE(p.Feed(good[i]));
    auto s=p.Feed(good[89]); ASSERT_TRUE(s);
    EXPECT_EQ(s->percent,10); EXPECT_EQ(s->voltage_mv,4000);
    EXPECT_EQ(s->current_ma,-1); EXPECT_EQ(s->capacity_mah,5000);
    EXPECT_EQ(s->remaining_mah,500); EXPECT_TRUE(s->discharging);
    for(auto v:Frame(101)) EXPECT_FALSE(p.Feed(v));
    for(auto v:Frame(0)) s=p.Feed(v);
    ASSERT_TRUE(s); EXPECT_EQ(s->percent,0);
}

TEST(BmsReaderTest, ReadsSerialAndPublishesDisconnectedOnShutdown) {
    const int master=posix_openpt(O_RDWR|O_NOCTTY|O_NONBLOCK);
    ASSERT_GE(master,0); ASSERT_EQ(grantpt(master),0); ASSERT_EQ(unlockpt(master),0);
    const std::string port=ptsname(master);
    char temp[]="/tmp/ugripper-bms-test-XXXXXX";
    ASSERT_NE(mkdtemp(temp),nullptr);
    const auto path=std::filesystem::path(temp)/"battery.json";
    {
        BmsReader reader(port,path.string());
        pollfd poller{master,POLLIN,0}; ASSERT_GT(poll(&poller,1,2000),0);
        uint8_t request[32]; EXPECT_EQ(read(master,request,sizeof(request)),12);
        const auto frame=Frame(5);
        ASSERT_EQ(write(master,frame.data(),15),15);
        ASSERT_EQ(write(master,frame.data()+15,75),75);
        const auto deadline=BatterySteadyMs()+2000;
        while(!reader.Sample() && BatterySteadyMs()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ASSERT_TRUE(reader.Sample()); EXPECT_EQ(reader.Sample()->percent,5);
    }
    nlohmann::json status; std::ifstream(path)>>status;
    EXPECT_FALSE(status.at("connected").get<bool>());
    close(master); std::filesystem::remove_all(temp);
}

TEST(EgoSpeechPolicyTest, WritingDelayIsCancelledAndDoesNotFireForUnmount) {
    EgoSpeechPolicy p;
    p.SetPhase(EgoSpeechPolicy::Phase::Writing,100);
    EXPECT_FALSE(p.Next(4000,{},false));
    p.SetPhase(EgoSpeechPolicy::Phase::Recording,5000);
    p.SetPhase(EgoSpeechPolicy::Phase::Idle,5100);
    p.SetPhase(EgoSpeechPolicy::Phase::Writing,5200);
    EXPECT_FALSE(p.Next(8200,{},false));
    EXPECT_EQ(p.Next(8201,{},false),1003);
    EXPECT_FALSE(p.Next(9000,{},false));
    p.SetPhase(EgoSpeechPolicy::Phase::Recording,10000);
    p.SetPhase(EgoSpeechPolicy::Phase::Writing,11000);
    p.SetPhase(EgoSpeechPolicy::Phase::Idle,12000);
    EXPECT_FALSE(p.Next(15000,{},false));
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
