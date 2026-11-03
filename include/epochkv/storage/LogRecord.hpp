#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace epochkv
{

    constexpr std::size_t MAX_KEY_BYTES = 256;
    constexpr std::size_t MAX_VALUE_BYTES = 4096;

    enum class LogRecordType : std::uint8_t
    {
        Put = 1,
        Delete = 2,
        Commit = 3
    };

    // represents a single record in the log
    // uint64_t used to ensure seq. number has a predictable size regardless of platform
    struct LogRecord
    {
        LogRecordType type;

        std::uint64_t sequence_number;
        std::string key;
        std::string value;
    };

}