#include "epochkv/replication/BackupStore.hpp"
#include "epochkv/network/MessageIO.hpp"
#include "epochkv/network/Port.hpp"
#include "epochkv/network/ReplicationIO.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace
{

    constexpr int LISTEN_BACKLOG = 8;

    int createListeningSocket(std::uint16_t port)
    {
        int listening_socket = ::socket(AF_INET, SOCK_STREAM, 0);

        if (listening_socket == -1)
        {
            throw std::system_error(errno, std::generic_category(), "Failed to create backup socket");
        }

        try
        {
            int reuse_address = 1;

            if (::setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR,
                             &reuse_address, sizeof(reuse_address)) == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Failed to configure backup socket");
            }

            sockaddr_in server_address{};

            server_address.sin_family = AF_INET;
            server_address.sin_port = htons(port);
            server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            if (::bind(listening_socket, reinterpret_cast<sockaddr *>(&server_address),
                       sizeof(server_address)) == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Failed to bind backup socket");
            }

            if (::listen(listening_socket, LISTEN_BACKLOG) == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Failed to listen for replication");
            }
        }

        catch (...)
        {
            ::close(listening_socket);

            throw;
        }

        return listening_socket;
    }

    void handleConnection(int client_socket, epochkv::BackupStore &backup_store)
    {
        try
        {
            while (true)
            {
                auto request_type = epochkv::receiveMessage(client_socket);

                if (!request_type)
                {
                    return;
                }

                if (*request_type == "STATUS")
                {
                    epochkv::BackupStatus backup_status = backup_store.status();

                    epochkv::sendMessage(client_socket, "STATUS");
                    epochkv::sendMessage(client_socket, std::to_string(backup_status.last_committed_sequence));
                    epochkv::sendMessage(client_socket, std::to_string(backup_status.pending_sequence));

                    continue;
                }

                if (*request_type == "HISTORY")
                {
                    std::vector<epochkv::LogRecord> records = backup_store.history();

                    epochkv::sendMessage(client_socket, "HISTORY");

                    for (const epochkv::LogRecord &record : records)
                    {
                        epochkv::sendReplicationRecord(client_socket, record);
                    }

                    epochkv::sendMessage(client_socket, "END");

                    continue;
                }

                epochkv::LogRecord record = epochkv::receiveReplicationRecord(client_socket, *request_type);

                std::string response;

                try
                {
                    response = backup_store.processRecord(record);
                }

                catch (const std::exception &error)
                {
                    epochkv::sendMessage(client_socket, "ERROR " + std::string(error.what()));

                    return;
                }

                epochkv::sendMessage(client_socket, response);
            }
        }

        catch (const std::exception &error)
        {
            std::cerr << "Replication connection failed: "
                      << error.what() << '\n';
        }
    }

}

int main(int argument_count, char *arguments[])
{
    if (argument_count != 3)
    {
        std::cerr << "Usage: epochkv_backup <port> <log-file>\n";

        return EXIT_FAILURE;
    }

    try
    {
        std::signal(SIGPIPE, SIG_IGN);

        std::uint16_t port = epochkv::parsePort(arguments[1]);
        std::filesystem::path log_path = arguments[2];

        if (log_path.has_parent_path())
        {
            std::filesystem::create_directories(log_path.parent_path());
        }

        epochkv::BackupStore backup_store(log_path);

        int listening_socket = createListeningSocket(port);

        std::cout << "EpochKV backup listening on 127.0.0.1:" << port << '\n';

        std::cout.flush();

        while (true)
        {
            int client_socket = ::accept(listening_socket, nullptr, nullptr);

            if (client_socket == -1)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                int error_number = errno;

                ::close(listening_socket);

                throw std::system_error(error_number, std::generic_category(),
                                        "Failed to accept replication connection");
            }

            handleConnection(client_socket, backup_store);

            ::close(client_socket);

            if (backup_store.requiresRestart())
            {
                ::close(listening_socket);

                throw std::runtime_error("Backup stopped after a failed log write");
            }
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';

        return EXIT_FAILURE;
    }
}