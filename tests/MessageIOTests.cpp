#include "epochkv/network/MessageIO.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testMessageRoundTrip()
{
    int sockets[2];

    require(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            "socket pair is created");

    epochkv::sendMessage(sockets[0], "first");
    epochkv::sendMessage(sockets[0], "second");

    require(epochkv::receiveMessage(sockets[1]) == "first",
            "first message is received");
    require(epochkv::receiveMessage(sockets[1]) == "second",
            "messages keep their boundaries");

    epochkv::sendMessage(sockets[0], "");

    require(epochkv::receiveMessage(sockets[1]) == "",
            "empty message is preserved");

    std::string binary_message("A\0B", 3);
    epochkv::sendMessage(sockets[0], binary_message);

    require(epochkv::receiveMessage(sockets[1]) == binary_message,
            "binary bytes are preserved");

    ::close(sockets[0]);

    require(!epochkv::receiveMessage(sockets[1]).has_value(),
            "clean disconnect is recognized");

    ::close(sockets[1]);
}

void testInterruptedMessage()
{
    int sockets[2];

    require(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            "socket pair is created");

    std::uint32_t network_length = htonl(5);

    require(::send(sockets[0], &network_length, sizeof(network_length), 0) == sizeof(network_length),
            "message header is sent");

    require(::send(sockets[0], "ab", 2, 0) == 2,
            "partial message body is sent");

    ::close(sockets[0]);

    bool rejected = false;

    try
    {
        epochkv::receiveMessage(sockets[1]);
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }

    require(rejected, "incomplete message is rejected");

    ::close(sockets[1]);
}

void testOversizedMessage()
{
    int sockets[2];

    require(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            "socket pair is created");

    bool rejected = false;

    try
    {
        epochkv::sendMessage(
            sockets[0],
            std::string(epochkv::MAX_MESSAGE_BYTES + 1, 'x'));
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }

    require(rejected, "oversized outgoing message is rejected");

    std::uint32_t invalid_length = htonl(
        static_cast<std::uint32_t>(epochkv::MAX_MESSAGE_BYTES + 1));

    require(::send(sockets[0], &invalid_length, sizeof(invalid_length), 0) == sizeof(invalid_length),
            "oversized message header is sent");

    rejected = false;

    try
    {
        epochkv::receiveMessage(sockets[1]);
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }

    require(rejected, "oversized incoming message is rejected");

    ::close(sockets[0]);
    ::close(sockets[1]);
}

int main()
{
    testMessageRoundTrip();
    testInterruptedMessage();
    testOversizedMessage();

    std::cout << "MessageIO tests passed\n";
}