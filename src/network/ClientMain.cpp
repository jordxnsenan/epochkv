#include "epochkv/network/MessageIO.hpp"
#include "epochkv/network/Port.hpp"

#include <cerrno>
#include <csignal>
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

    constexpr int PUT_ARGUMENT_COUNT = 5;
    constexpr int KEY_ARGUMENT_COUNT = 4;

    void printUsage()
    {
        std::cerr << "Usage:\n"
                  << "  epochkv_client <port> put <key> <value>\n"
                  << "  epochkv_client <port> get <key>\n"
                  << "  epochkv_client <port> delete <key>\n";
    }

    int connectToServer(std::uint16_t port)
    {
        int server_socket = ::socket(AF_INET, SOCK_STREAM, 0);

        if (server_socket == -1)
        {
            throw std::system_error(errno, std::generic_category(), "Failed to create client socket");
        }

        sockaddr_in server_address{};

        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(port);
        server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::connect(server_socket,
                      reinterpret_cast<sockaddr *>(&server_address),
                      sizeof(server_address)) == -1)
        {
            int error_number = errno;

            ::close(server_socket);

            throw std::system_error(error_number, std::generic_category(), "Failed to connect to server");
        }

        return server_socket;
    }

}

int main(int argument_count, char *arguments[])
{
    if (argument_count < KEY_ARGUMENT_COUNT)
    {
        printUsage();

        return EXIT_FAILURE;
    }

    std::string command = arguments[2];

    bool is_put_command = command == "put";
    bool is_key_command = command == "get" || command == "delete";

    bool has_valid_arguments =
        (is_put_command && argument_count == PUT_ARGUMENT_COUNT) ||
        (is_key_command && argument_count == KEY_ARGUMENT_COUNT);

    if (!has_valid_arguments)
    {
        printUsage();

        return EXIT_FAILURE;
    }

    std::string key = arguments[3];

    if (key.empty())
    {
        std::cerr << "Error: key cannot be empty\n";

        return EXIT_FAILURE;
    }

    try
    {
        std::signal(SIGPIPE, SIG_IGN);

        std::uint16_t port = epochkv::parsePort(arguments[1]);

        int server_socket = connectToServer(port);

        std::string response;

        try
        {
            epochkv::sendMessage(server_socket, command);
            epochkv::sendMessage(server_socket, key);

            if (is_put_command)
            {
                epochkv::sendMessage(server_socket, arguments[4]);
            }

            auto received_response = epochkv::receiveMessage(server_socket);

            if (!received_response)
            {
                throw std::runtime_error("Server disconnected without a response");
            }

            response = *received_response;
        }
        catch (...)
        {
            ::close(server_socket);

            throw;
        }

        ::close(server_socket);

        if (response.starts_with("ERROR "))
        {
            std::cerr << response << '\n';

            return EXIT_FAILURE;
        }

        std::cout << response << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}