#include "epochkv/replication/BackupClient.hpp"
#include "epochkv/network/MessageIO.hpp"
#include "epochkv/network/ReplicationIO.hpp"

#include <cerrno>
#include <stdexcept>
#include <string>
#include <system_error>
#include <charconv>
#include <limits>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace epochkv
{
    namespace
    {

        constexpr int BACKUP_IO_TIMEOUT_SECONDS = 3;

        int connectToBackup(std::uint16_t port)
        {
            int socket_descriptor = ::socket(AF_INET, SOCK_STREAM, 0);

            if (socket_descriptor == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Failed to create backup socket");
            }

            try
            {
                timeval timeout{};
                timeout.tv_sec = BACKUP_IO_TIMEOUT_SECONDS;

                if (::setsockopt(socket_descriptor, SOL_SOCKET, SO_SNDTIMEO,
                                 &timeout, sizeof(timeout)) == -1 ||
                    ::setsockopt(socket_descriptor, SOL_SOCKET, SO_RCVTIMEO,
                                 &timeout, sizeof(timeout)) == -1)
                {
                    throw std::system_error(errno, std::generic_category(), "Failed to configure socket timeout");
                }

#ifdef SO_NOSIGPIPE
                // ensures that on macOS EPIPE  is returned instead of raising SIGPIPE when backup disconnects
                int disable_sigpipe = 1;

                if (::setsockopt(socket_descriptor, SOL_SOCKET, SO_NOSIGPIPE,
                                 &disable_sigpipe, sizeof(disable_sigpipe)) == -1)
                {
                    throw std::system_error(errno, std::generic_category(), "Failed to configure backup socket");
                }
#endif

                sockaddr_in backup_address{};

                backup_address.sin_family = AF_INET;
                backup_address.sin_port = htons(port);
                backup_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

                if (::connect(socket_descriptor, reinterpret_cast<sockaddr *>(&backup_address),
                              sizeof(backup_address)) == -1)
                {
                    throw std::system_error(errno, std::generic_category(), "Failed to connect to backup");
                }
            }

            catch (...)
            {
                ::close(socket_descriptor);

                throw;
            }

            return socket_descriptor;
        }

        std::uint64_t parseStatusNumber(const std::string &text)
        {
            std::uint64_t number = 0;

            auto result = std::from_chars(text.data(), text.data() + text.size(), number);

            if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
            {
                throw std::runtime_error("Backup returned an invalid sequence number");
            }

            return number;
        }

    }

    BackupClient::BackupClient(std::uint16_t backup_port)
        : backup_port_(backup_port)
    {
        if (backup_port_ == 0)
        {
            throw std::invalid_argument("Backup port cannot be zero");
        }
    }

    void BackupClient::prepare(const LogRecord &operation) const
    {
        if (operation.type != LogRecordType::Put &&
            operation.type != LogRecordType::Delete)
        {
            throw std::invalid_argument("Only PUT and DELETE operations can be prepared");
        }

        sendAndCheck(operation, "PREPARED");
    }

    void BackupClient::commit(std::uint64_t sequence_number) const
    {
        sendAndCheck({LogRecordType::Commit, sequence_number, "", ""}, "COMMITTED");
    }

    void BackupClient::sendAndCheck(const LogRecord &record,
                                    std::string_view expected_response) const
    {
        int socket_descriptor = connectToBackup(backup_port_);

        try
        {
            sendReplicationRecord(socket_descriptor, record);

            auto response = receiveMessage(socket_descriptor);

            if (!response)
            {
                throw std::runtime_error("Backup disconnected without an acknowledgment");
            }

            if (*response != expected_response)
            {
                throw std::runtime_error("Unexpected backup response: " + *response);
            }
        }
        catch (...)
        {
            ::close(socket_descriptor);

            throw;
        }

        ::close(socket_descriptor);
    }

    BackupStatus BackupClient::status() const
    {
        int socket_descriptor = connectToBackup(backup_port_);

        BackupStatus backup_status;

        try
        {
            sendMessage(socket_descriptor, "STATUS");

            auto response_type = receiveMessage(socket_descriptor);

            if (!response_type || *response_type != "STATUS")
            {
                throw std::runtime_error("Unexpected backup status response");
            }

            auto committed_text = receiveMessage(socket_descriptor);
            auto pending_text = receiveMessage(socket_descriptor);

            if (!committed_text || !pending_text)
            {
                throw std::runtime_error("Incomplete backup status response");
            }

            backup_status.last_committed_sequence = parseStatusNumber(*committed_text);
            backup_status.pending_sequence = parseStatusNumber(*pending_text);

            if (backup_status.pending_sequence != 0)
            {
                bool sequence_overflow = (backup_status.last_committed_sequence == std::numeric_limits<std::uint64_t>::max());

                if (sequence_overflow ||
                    backup_status.pending_sequence !=
                        backup_status.last_committed_sequence + 1)
                {
                    throw std::runtime_error("Backup reported inconsistent sequences");
                }
            }
        }
        catch (...)
        {
            ::close(socket_descriptor);

            throw;
        }

        ::close(socket_descriptor);

        return backup_status;
    }

    std::vector<LogRecord> BackupClient::history() const
    {
        int socket_descriptor = connectToBackup(backup_port_);

        std::vector<LogRecord> records;

        try
        {
            sendMessage(socket_descriptor, "HISTORY");

            auto response_type = receiveMessage(socket_descriptor);

            if (!response_type || *response_type != "HISTORY")
            {
                throw std::runtime_error("Unexpected backup history response");
            }

            while (true)
            {
                auto record_type = receiveMessage(socket_descriptor);

                if (!record_type)
                {
                    throw std::runtime_error("Connection closed during history transfer");
                }

                if (*record_type == "END")
                {
                    break;
                }

                LogRecord record = receiveReplicationRecord(socket_descriptor, *record_type);

                records.push_back(record);
            }
        }

        catch (...)
        {
            ::close(socket_descriptor);

            throw;
        }

        ::close(socket_descriptor);

        return records;
    }

}
