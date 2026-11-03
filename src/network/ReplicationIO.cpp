#include "epochkv/network/ReplicationIO.hpp"
#include "epochkv/network/MessageIO.hpp"

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>

namespace epochkv
{
    namespace
    {

        std::string recordTypeToText(LogRecordType type)
        {
            switch (type)
            {
            case LogRecordType::Put:
                return "PREPARE_PUT";
            case LogRecordType::Delete:
                return "PREPARE_DELETE";
            case LogRecordType::Commit:
                return "COMMIT";
            }

            throw std::invalid_argument("Unknown replication record type");
        }

        LogRecordType textToRecordType(const std::string &type_text)
        {
            if (type_text == "PREPARE_PUT")
            {
                return LogRecordType::Put;
            }

            if (type_text == "PREPARE_DELETE")
            {
                return LogRecordType::Delete;
            }

            if (type_text == "COMMIT")
            {
                return LogRecordType::Commit;
            }

            throw std::runtime_error("Unknown replication record type");
        }

        void validateReplicationRecord(const LogRecord &record)
        {
            if (record.sequence_number == 0)
            {
                throw std::invalid_argument("Replication sequence number must be positive");
            }

            if (record.key.size() > MAX_KEY_BYTES || record.value.size() > MAX_VALUE_BYTES)
            {
                throw std::invalid_argument("Replication record exceeds size limits");
            }

            if (record.type == LogRecordType::Commit)
            {
                if (!record.key.empty() || !record.value.empty())
                {
                    throw std::invalid_argument("Commit record cannot contain a key or value");
                }

                return;
            }

            if (record.type != LogRecordType::Put && record.type != LogRecordType::Delete)
            {
                throw std::invalid_argument("Unknown replication record type");
            }

            if (record.key.empty())
            {
                throw std::invalid_argument("Replication key cannot be empty");
            }

            if (record.type == LogRecordType::Delete && !record.value.empty())
            {
                throw std::invalid_argument("Delete record cannot contain a value");
            }
        }

        std::string readRequiredMessage(int socket_descriptor)
        {
            auto message = receiveMessage(socket_descriptor);

            if (!message)
            {
                throw std::runtime_error("Connection closed during a replication record");
            }

            return *message;
        }

        std::uint64_t parseSequenceNumber(const std::string &sequence_text)
        {
            std::uint64_t sequence_number = 0;

            const char *first_character = sequence_text.data();
            const char *last_character = first_character + sequence_text.size();

            auto result = std::from_chars(
                first_character,
                last_character,
                sequence_number);

            if (result.ec != std::errc{} ||
                result.ptr != last_character ||
                sequence_number == 0)
            {
                throw std::runtime_error("Invalid replication sequence number");
            }

            return sequence_number;
        }

    }

    void sendReplicationRecord(int socket_descriptor, const LogRecord &record)
    {
        validateReplicationRecord(record);

        sendMessage(socket_descriptor, recordTypeToText(record.type));
        sendMessage(socket_descriptor, std::to_string(record.sequence_number));
        sendMessage(socket_descriptor, record.key);
        sendMessage(socket_descriptor, record.value);
    }

    std::optional<LogRecord> receiveReplicationRecord(int socket_descriptor)
    {
        auto type_text = receiveMessage(socket_descriptor);

        if (!type_text)
        {
            return std::nullopt;
        }

        LogRecordType type = textToRecordType(*type_text);

        std::uint64_t sequence_number = parseSequenceNumber(
            readRequiredMessage(socket_descriptor));

        std::string key = readRequiredMessage(socket_descriptor);
        std::string value = readRequiredMessage(socket_descriptor);

        LogRecord record{type, sequence_number, key, value};

        validateReplicationRecord(record);

        return record;
    }
}