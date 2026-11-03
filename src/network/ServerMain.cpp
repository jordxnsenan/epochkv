#include "epochkv/Database.hpp"
#include "epochkv/network/MessageIO.hpp"
#include "epochkv/network/Port.hpp"

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

namespace
{

    constexpr int LISTEN_BACKLOG = 8;

    int createListeningSocket(std::uint16_t port)
    {
        int listening_socket = ::socket(AF_INET, SOCK_STREAM, 0);

        if (listening_socket == -1)
        {
            throw std::system_error(errno, std::generic_category(), "Failed to create server socket");
        }

        try
        {
            int reuse_address = 1;

            if (::setsockopt(listening_socket,
                             SOL_SOCKET, SO_REUSEADDR,
                             &reuse_address,
                             sizeof(reuse_address)) == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Failed to configure server socket");
            }

            sockaddr_in server_address{};

            server_address.sin_family = AF_INET;
            server_address.sin_port = htons(port);
            server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            if (::bind(listening_socket,
                       reinterpret_cast<sockaddr *>(&server_address),
                       sizeof(server_address)) == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Failed to bind server socket");
            }

            if (::listen(listening_socket, LISTEN_BACKLOG) == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Failed to listen for clients");
            }
        }

        catch (...)
        {
            ::close(listening_socket);

            throw;
        }

        return listening_socket;
    }

    std::string readRequiredMessage(int client_socket)
    {
        auto message = epochkv::receiveMessage(client_socket);

        if (!message)
        {
            throw std::runtime_error("Client disconnected before completing the request");
        }

        return *message;
    }

    std::string executeCommand(int client_socket, epochkv::Database &database,
                               const std::string &command)
    {
        if (command != "put" && command != "get" && command != "delete")
        {
            return "ERROR Unknown command";
        }

        std::string key = readRequiredMessage(client_socket);

        if (key.empty())
        {
            return "ERROR Key cannot be empty";
        }

        if (command == "put")
        {
            std::string value = readRequiredMessage(client_socket);

            database.put(key, value);

            return "OK";
        }

        if (command == "get")
        {
            auto value = database.get(key);

            return value ? "VALUE " + *value : "NOT_FOUND";
        }

        return database.erase(key) ? "OK" : "NOT_FOUND";
    }

    void handleClient(int client_socket, epochkv::Database &database)
    {
        try
        {
            auto command = epochkv::receiveMessage(client_socket);

            if (!command)
            {
                return;
            }

            std::string response;

            try
            {
                response = executeCommand(client_socket, database, *command);
            }

            catch (const std::exception &error)
            {
                response = "ERROR " + std::string(error.what());
            }

            epochkv::sendMessage(client_socket, response);
        }
        catch (const std::exception &error)
        {
            std::cerr << "Client connection failed: " << error.what() << '\n';
        }
    }

}

int main(int argument_count, char *arguments[])
{
    if (argument_count != 3)
    {
        std::cerr << "Usage: epochkv_server <port> <log-file>\n";

        return EXIT_FAILURE;
    }

    try
    {
        // ensures disconnected clients don't terminate the server process
        std::signal(SIGPIPE, SIG_IGN);

        std::uint16_t port = epochkv::parsePort(arguments[1]);
        std::filesystem::path log_path = arguments[2];

        if (log_path.has_parent_path())
        {
            std::filesystem::create_directories(log_path.parent_path());
        }

        epochkv::Database database(log_path);

        int listening_socket = createListeningSocket(port);

        std::cout << "EpochKV listening on 127.0.0.1:" << port << '\n';

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
                                        "Failed to accept client");
            }

            handleClient(client_socket, database);

            ::close(client_socket);
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';

        return EXIT_FAILURE;
    }
}