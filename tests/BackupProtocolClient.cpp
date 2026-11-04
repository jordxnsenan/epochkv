#include "epochkv/network/MessageIO.hpp"
#include "epochkv/network/Port.hpp"
#include "epochkv/network/ReplicationIO.hpp"

#include <charconv>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

    constexpr int PROBE_ARGUMENT_COUNT = 3;
    constexpr int COMMIT_ARGUMENT_COUNT = 4;
    constexpr int DELETE_ARGUMENT_COUNT = 5;
    constexpr int PUT_ARGUMENT_COUNT = 6;

    std::uint64_t parseSequenceNumber(const std::string &text)
    {
        std::uint64_t sequence_number = 0;

        auto result = std::from_chars(
            text.data(),
            text.data() + text.size(),
            sequence_number);

        if (result.ec != std::errc{} ||
            result.ptr != text.data() + text.size() ||
            sequence_number == 0)
        {
            throw std::invalid_argument("Invalid sequence number");
        }

        return sequence_number;
    }

    int connectToBackup(std::uint16_t port)
    {
        int socket_descriptor = ::socket(AF_INET, SOCK_STREAM, 0);

        if (socket_descriptor == -1)
        {
            throw std::system_error(errno, std::generic_category(), "Failed to create socket");
        }

        sockaddr_in address{};

        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::connect(socket_descriptor, reinterpret_cast<sockaddr *>(&address),
                      sizeof(address)) == -1)
        {
            int error_number = errno;

            ::close(socket_descriptor);

            throw std::system_error(error_number, std::generic_category(), "Failed to connect");
        }

        return socket_descriptor;
    }

}

int main(int argument_count, char *arguments[])
{
    if (argument_count < PROBE_ARGUMENT_COUNT)
    {
        std::cerr << "Usage: backup_protocol_client <port> <probe|put|delete|commit> [sequence] [key] [value]\n";

        return EXIT_FAILURE;
    }

    std::string command = arguments[2];

    bool is_probe = command == "probe";
    bool is_put = command == "put";
    bool is_delete = command == "delete";
    bool is_commit = command == "commit";

    bool valid_arguments =
        (is_probe && argument_count == PROBE_ARGUMENT_COUNT) ||
        (is_put && argument_count == PUT_ARGUMENT_COUNT) ||
        (is_delete && argument_count == DELETE_ARGUMENT_COUNT) ||
        (is_commit && argument_count == COMMIT_ARGUMENT_COUNT);

    if (!valid_arguments)
    {
        std::cerr << "Invalid replication command or arguments\n";

        return EXIT_FAILURE;
    }

    try
    {
        std::signal(SIGPIPE, SIG_IGN);

        std::uint16_t port = epochkv::parsePort(arguments[1]);

        if (is_probe)
        {
            int socket_descriptor = connectToBackup(port);

            ::close(socket_descriptor);

            return EXIT_SUCCESS;
        }

        epochkv::LogRecordType record_type =
            is_put ? epochkv::LogRecordType::Put : is_delete ? epochkv::LogRecordType::Delete
                                                             : epochkv::LogRecordType::Commit;

        epochkv::LogRecord record{
            record_type,
            parseSequenceNumber(arguments[3]),
            (is_put || is_delete) ? arguments[4] : "",
            is_put ? arguments[5] : ""};

        int socket_descriptor = connectToBackup(port);

        std::string response;

        try
        {
            epochkv::sendReplicationRecord(socket_descriptor, record);

            auto received_response = epochkv::receiveMessage(socket_descriptor);

            if (!received_response)
            {
                throw std::runtime_error("Backup closed without responding");
            }

            response = *received_response;
        }

        catch (...)
        {
            ::close(socket_descriptor);

            throw;
        }

        ::close(socket_descriptor);

        std::cout << response << '\n';

        return response.starts_with("ERROR ") ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';

        return EXIT_FAILURE;
    }
}