#pragma once

#include "epochkv/storage/LogRecord.hpp"

#include <filesystem>
#include <vector>

namespace epochkv
{

    class LogStore
    {
    public:
        explicit LogStore(const std::filesystem::path &log_path);

        void append(const LogRecord &record);
        std::vector<LogRecord> readAll();

    private:
        std::filesystem::path log_path_;
    };

}