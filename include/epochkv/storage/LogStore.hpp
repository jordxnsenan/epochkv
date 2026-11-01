#pragma once

#include "epochkv/storage/LogRecord.hpp"

#include <filesystem>

namespace epochkv
{

    class LogStore
    {
    public:
        explicit LogStore(const std::filesystem::path &log_path);

        void append(const LogRecord &record);

    private:
        std::filesystem::path log_path_;
    };

}