#include "epochkv/network/MessageIO.hpp"

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <system_error>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

namespace epochkv
{
    namespace
    {

        void sendAll(int socket_descriptor, const char *data, std::size_t byte_count)
        {
            std::size_t total_bytes_sent = 0;

            while (total_bytes_sent < byte_count)
            {
                ssize_t bytes_sent = ::send(
                    socket_descriptor,
                    data + total_bytes_sent,
                    byte_count - total_bytes_sent,
                    0);

                if (bytes_sent == -1 && errno == EINTR)
                {
                    continue;
                }

                if (bytes_sent == -1)
                {
                    throw std::system_error(errno, std::generic_category(), "Failed to send message");
                }

                if (bytes_sent == 0)
                {
                    throw std::runtime_error("Socket stopped sending data");
                }

                total_bytes_sent += static_cast<std::size_t>(bytes_sent);
            }
        }

        bool receiveAll(int socket_descriptor, char *buffer, std::size_t byte_count)
        {
            std::size_t total_bytes_received = 0;

            while (total_bytes_received < byte_count)
            {
                ssize_t bytes_received = ::recv(
                    socket_descriptor,
                    buffer + total_bytes_received,
                    byte_count - total_bytes_received,
                    0);

                if (bytes_received == -1 && errno == EINTR)
                {
                    continue;
                }

                if (bytes_received == -1)
                {
                    throw std::system_error(errno, std::generic_category(), "Failed to receive message");
                }

                if (bytes_received == 0)
                {
                    if (total_bytes_received == 0)
                    {
                        return false;
                    }

                    throw std::runtime_error("Connection closed during a message");
                }

                total_bytes_received += static_cast<std::size_t>(bytes_received);
            }

            return true;
        }

    }

    void sendMessage(int socket_descriptor, std::string_view message)
    {
        if (message.size() > MAX_MESSAGE_BYTES)
        {
            throw std::invalid_argument("Message exceeds maximum allowed size");
        }

        // uses network byte order to keep length consistent across different devices
        std::uint32_t network_length = htonl(static_cast<std::uint32_t>(message.size()));

        sendAll(socket_descriptor, reinterpret_cast<const char *>(&network_length), sizeof(network_length));
        sendAll(socket_descriptor, message.data(), message.size());
    }

    std::optional<std::string> receiveMessage(int socket_descriptor)
    {
        std::uint32_t network_length = 0;

        if (!receiveAll(socket_descriptor, reinterpret_cast<char *>(&network_length), sizeof(network_length)))
        {
            return std::nullopt;
        }

        std::uint32_t message_length = ntohl(network_length);

        if (message_length > MAX_MESSAGE_BYTES)
        {
            throw std::runtime_error("Incoming message exceeds maximum allowed size");
        }

        std::string message(message_length, '\0');

        if (message_length > 0 && !receiveAll(socket_descriptor, message.data(), message_length))
        {
            throw std::runtime_error("Connection closed before message body");
        }

        return message;
    }

}