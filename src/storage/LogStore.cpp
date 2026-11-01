#include "epochkv/storage/LogStore.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

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

}