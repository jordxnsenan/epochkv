#include "epochkv/storage/LogStore.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <array>

#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace epochkv
{
    namespace
    {

        constexpr std::size_t MAX_KEY_BYTES = 256;
        constexpr std::size_t MAX_VALUE_BYTES = 4096;

        constexpr std::uint8_t RECORD_FORMAT_VERSION = 1;

        // used to extract individual bytes when encoding ints.
        constexpr unsigned int BITS_PER_BYTE = 8;
        constexpr std::uint64_t BYTE_MASK = 0xFF;

        // standard constants for the 32-bit FNV-1a hash algorithm
        // The hash helps detect accidental corruption in stored log records
        // source: https://mojoauth.com/security-guides/fnv-1a-in-nodejs
        constexpr std::uint32_t FNV1A_OFFSET_BASIS = 2166136261u;
        constexpr std::uint32_t FNV1A_PRIME = 16777619u;

        constexpr std::size_t FIXED_PAYLOAD_BYTES =
            sizeof(std::uint8_t) * 2 + sizeof(std::uint64_t) + sizeof(std::uint32_t) * 2;

        constexpr std::size_t MAX_PAYLOAD_BYTES =
            FIXED_PAYLOAD_BYTES + MAX_KEY_BYTES + MAX_VALUE_BYTES;

        void appendNumber(std::string &bytes, std::uint64_t number, std::size_t byte_count)
        {
            for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
            {
                bytes.push_back(static_cast<char>((number >> (byte_index * BITS_PER_BYTE)) & BYTE_MASK));
            }
        }

        std::uint32_t calculateChecksum(std::string_view bytes)
        {
            std::uint32_t checksum = FNV1A_OFFSET_BASIS;

            for (unsigned char byte : bytes)
            {
                checksum ^= byte;
                checksum *= FNV1A_PRIME;
            }

            return checksum;
        }

        void validateRecord(const LogRecord &record)
        {
            if (record.key.size() > MAX_KEY_BYTES || record.value.size() > MAX_VALUE_BYTES)
            {
                throw std::invalid_argument("Log record exceeds the size limit");
            }

            if (record.type == LogRecordType::Commit)
            {
                if (!record.key.empty() || !record.value.empty())
                {
                    throw std::invalid_argument("Commit records cannot contain a key or value");
                }
                return;
            }

            if (record.type != LogRecordType::Put && record.type != LogRecordType::Delete)
            {
                throw std::invalid_argument("Unknown log record type");
            }

            if (record.key.empty())
            {
                throw std::invalid_argument("Log record key cannot be empty");
            }

            if (record.type == LogRecordType::Delete && !record.value.empty())
            {
                throw std::invalid_argument("Delete records cannot contain a value");
            }
        }

        std::string encodeRecord(const LogRecord &record)
        {
            validateRecord(record);

            std::string payload;
            payload.push_back(static_cast<char>(RECORD_FORMAT_VERSION));
            payload.push_back(static_cast<char>(record.type));

            appendNumber(payload, record.sequence_number, sizeof(record.sequence_number));
            appendNumber(payload, record.key.size(), sizeof(std::uint32_t));
            appendNumber(payload, record.value.size(), sizeof(std::uint32_t));

            payload.append(record.key);
            payload.append(record.value);

            std::string encoded_record;
            appendNumber(encoded_record, payload.size(), sizeof(std::uint32_t));
            appendNumber(encoded_record, calculateChecksum(payload), sizeof(std::uint32_t));
            encoded_record.append(payload);

            return encoded_record;
        }

        void writeAll(int file_descriptor, const std::string &bytes)
        {
            std::size_t total_bytes_written = 0;

            while (total_bytes_written < bytes.size())
            {
                ssize_t bytes_written = ::write(
                    file_descriptor,
                    bytes.data() + total_bytes_written,
                    bytes.size() - total_bytes_written);

                if (bytes_written == -1 && errno == EINTR)
                {
                    continue;
                }

                if (bytes_written == -1)
                {
                    throw std::system_error(errno, std::generic_category(), "Failed to write log record");
                }

                if (bytes_written == 0)
                {
                    throw std::runtime_error("Writing a log record made no progress");
                }

                total_bytes_written += static_cast<std::size_t>(bytes_written);
            }
        }

        std::size_t readBytes(int file_descriptor, char *buffer, std::size_t byte_count)
        {
            std::size_t total_bytes_read = 0;

            while (total_bytes_read < byte_count)
            {
                ssize_t bytes_read = ::read(
                    file_descriptor,
                    buffer + total_bytes_read,
                    byte_count - total_bytes_read);

                if (bytes_read == -1 && errno == EINTR)
                {
                    continue;
                }

                if (bytes_read == -1)
                {
                    throw std::system_error(errno, std::generic_category(), "Failed to read log file");
                }

                if (bytes_read == 0)
                {
                    break;
                }

                total_bytes_read += static_cast<std::size_t>(bytes_read);
            }

            return total_bytes_read;
        }

        std::uint64_t readNumber(std::string_view bytes, std::size_t &position, std::size_t byte_count)
        {
            if (position > bytes.size() || bytes.size() - position < byte_count)
            {
                throw std::runtime_error("Log record is missing a numeric field");
            }

            std::uint64_t number = 0;

            for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index)
            {
                auto byte = static_cast<unsigned char>(bytes[position + byte_index]);

                number |= static_cast<std::uint64_t>(byte) << (byte_index * BITS_PER_BYTE);
            }

            position += byte_count;

            return number;
        }

        LogRecord decodeRecord(std::string_view payload)
        {
            if (payload.size() < FIXED_PAYLOAD_BYTES)
            {
                throw std::runtime_error("Log record is too short");
            }

            std::size_t position = 0;

            auto version = static_cast<std::uint8_t>(payload[position++]);
            auto type = static_cast<LogRecordType>(static_cast<std::uint8_t>(payload[position++]));

            if (version != RECORD_FORMAT_VERSION)
            {
                throw std::runtime_error("Unsupported log format version");
            }

            std::uint64_t sequence_number = readNumber(payload, position, sizeof(std::uint64_t));
            std::size_t key_length = readNumber(payload, position, sizeof(std::uint32_t));
            std::size_t value_length = readNumber(payload, position, sizeof(std::uint32_t));

            if (key_length > MAX_KEY_BYTES || value_length > MAX_VALUE_BYTES ||
                payload.size() - position != key_length + value_length)
            {
                throw std::runtime_error("Log record has invalid key or value lengths");
            }

            LogRecord record{
                type,
                sequence_number,
                std::string(payload.substr(position, key_length)),
                std::string(payload.substr(position + key_length, value_length))};

            validateRecord(record);

            return record;
        }

        void removeIncompleteTail(int file_descriptor, off_t valid_bytes)
        {
            // removes the unfinished record before any new records are appended
            if (::ftruncate(file_descriptor, valid_bytes) == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Failed to truncate incomplete log record");
            }

            if (::fsync(file_descriptor) == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Failed to synchronize repaired log");
            }
        }

    }

    LogStore::LogStore(const std::filesystem::path &log_path)
        : log_path_(log_path)
    {
    }

    void LogStore::append(const LogRecord &record)
    {
        std::string encoded_record = encodeRecord(record);

        int file_descriptor = ::open(
            log_path_.c_str(),
            O_WRONLY | O_CREAT | O_APPEND,
            S_IRUSR | S_IWUSR);

        if (file_descriptor == -1)
        {
            throw std::system_error(errno, std::generic_category(), "Failed to open log file");
        }

        try
        {
            writeAll(file_descriptor, encoded_record);

            if (::fsync(file_descriptor) == -1)
            {
                throw std::system_error(errno, std::generic_category(), "Failed to synchronize log file");
            }
        }
        catch (...)
        {
            ::close(file_descriptor);
            throw;
        }

        if (::close(file_descriptor) == -1)
        {
            throw std::system_error(errno, std::generic_category(), "Failed to close log file");
        }
    }

    std::vector<LogRecord> LogStore::readAll()
    {
        int file_descriptor = ::open(log_path_.c_str(), O_RDWR);

        if (file_descriptor == -1)
        {
            if (errno == ENOENT)
            {
                return {};
            }

            throw std::system_error(errno, std::generic_category(), "Failed to open log for recovery");
        }

        std::vector<LogRecord> records;
        off_t valid_bytes = 0;

        try
        {
            while (true)
            {
                std::array<char, sizeof(std::uint32_t) * 2> header{};

                std::size_t header_bytes_read = readBytes(
                    file_descriptor, header.data(), header.size());

                if (header_bytes_read == 0)
                {
                    break;
                }

                if (header_bytes_read < header.size())
                {
                    removeIncompleteTail(file_descriptor, valid_bytes);
                    break;
                }

                std::size_t position = 0;
                std::string_view header_bytes(header.data(), header.size());

                std::size_t payload_length = readNumber(
                    header_bytes, position, sizeof(std::uint32_t));

                std::uint32_t stored_checksum = readNumber(
                    header_bytes, position, sizeof(std::uint32_t));

                if (payload_length < FIXED_PAYLOAD_BYTES || payload_length > MAX_PAYLOAD_BYTES)
                {
                    throw std::runtime_error("Log record has an invalid length");
                }

                std::string payload(payload_length, '\0');

                std::size_t payload_bytes_read = readBytes(
                    file_descriptor, payload.data(), payload.size());

                if (payload_bytes_read < payload.size())
                {
                    removeIncompleteTail(file_descriptor, valid_bytes);
                    break;
                }

                if (calculateChecksum(payload) != stored_checksum)
                {
                    throw std::runtime_error("Log record checksum mismatch");
                }

                records.push_back(decodeRecord(payload));

                valid_bytes += static_cast<off_t>(header.size() + payload.size());
            }
        }
        catch (...)
        {
            ::close(file_descriptor);
            throw;
        }

        if (::close(file_descriptor) == -1)
        {
            throw std::system_error(errno, std::generic_category(), "Failed to close log after recovery");
        }

        return records;
    }

}