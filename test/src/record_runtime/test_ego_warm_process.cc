#include "record_runtime/subprocess_handle.h"
#include <gtest/gtest.h>
#include <array>
#include <csignal>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
using DA::Ugripper::SubprocessHandle;
using DA::Ugripper::ProcessStopMode;

class SocketPair
{
public:
    SocketPair() { if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds.data()) != 0) fds = {-1, -1}; }
    ~SocketPair() { for (int fd : fds) if (fd >= 0) ::close(fd); }
    void closeChild() { ::close(fds[1]); fds[1] = -1; }
    std::array<int, 2> fds;
};

std::string receive(int fd)
{
    timeval timeout{2, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    std::array<char, 256> data;
    const auto count = ::recv(fd, data.data(), data.size(), 0);
    return count > 0 ? std::string(data.data(), count) : std::string();
}

TEST(EgoWarmProcessTest, StartDispatchPreservesPidAndExistingStopOwnership)
{
    SocketPair socket;
    ASSERT_GE(socket.fds[0], 0);
    SubprocessHandle worker("warm-test");
    const std::string code = "import socket,sys,os,time; s=socket.socket(fileno=int(sys.argv[1])); "
        "s.send(str(os.getpid()).encode()); assert s.recv(65537)==b'START'; "
        "s.send(str(os.getpid()).encode()); s.close(); time.sleep(30)";
    ASSERT_TRUE(worker.Start({"/usr/bin/python3", "-c", code, std::to_string(socket.fds[1])}, {socket.fds[1]}));
    socket.closeChild();
    const auto expected = std::to_string(worker.GetPid());
    EXPECT_EQ(receive(socket.fds[0]), expected);
    ASSERT_EQ(::send(socket.fds[0], "START", 5, MSG_NOSIGNAL | MSG_DONTWAIT), 5);
    EXPECT_EQ(receive(socket.fds[0]), expected);
    ASSERT_TRUE(worker.SendSignal(SIGTERM));
    EXPECT_TRUE(worker.Wait(1000));
}

TEST(EgoWarmProcessTest, UnrelatedChildCannotKeepIdleControlChannelOpen)
{
    SocketPair socket;
    SubprocessHandle worker("warm-test"), unrelated("unrelated-test");
    const std::string code = "import socket,sys; s=socket.socket(fileno=int(sys.argv[1])); "
        "s.send(b'ready'); assert s.recv(65537)==b''";
    ASSERT_TRUE(worker.Start({"/usr/bin/python3", "-c", code, std::to_string(socket.fds[1])}, {socket.fds[1]}));
    socket.closeChild();
    ASSERT_EQ(receive(socket.fds[0]), "ready");
    ASSERT_TRUE(unrelated.Start({"/bin/sleep", "30"}));
    ::close(socket.fds[0]); socket.fds[0] = -1;
    EXPECT_TRUE(worker.Wait(1000));
    EXPECT_EQ(worker.GetLastExitCode(), 0);
    EXPECT_TRUE(unrelated.IsRunning());
}

TEST(EgoWarmProcessTest, DeadWarmProcessRejectsCommandAndReplacementHasIndependentOwnership)
{
    SocketPair oldSocket;
    SubprocessHandle worker("warm-test");
    ASSERT_TRUE(worker.Start({"/bin/sleep", "30"}));
    const auto previous = worker.GetPid();
    ASSERT_TRUE(worker.Stop(ProcessStopMode::SigTermThenKill, 500));
    // Both inherited-control ownership and SIGPIPE suppression remain explicit.
    oldSocket.closeChild();
    EXPECT_EQ(::send(oldSocket.fds[0], "START", 5, MSG_NOSIGNAL | MSG_DONTWAIT), -1);
    ASSERT_TRUE(worker.Start({"/bin/sleep", "30"}));
    EXPECT_NE(worker.GetPid(), previous);
    EXPECT_TRUE(worker.Stop(ProcessStopMode::SigTermThenKill, 500));
}
} // namespace
