#pragma once

#include "epochkv/storage/KeyValueStore.hpp"
#include "epochkv/storage/LogStore.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace epochkv
{

    class Database
    {
    public:
        explicit Database(const std::filesystem::path &log_path);

        void put(const std::string &key, const std::string &value);
        std::optional<std::string> get(const std::string &key) const;
        bool erase(const std::string &key);

    private:
        LogStore log_store_;
        KeyValueStore key_value_store_;
        std::uint64_t next_sequence_number_ = 1;
        bool requires_restart_ = false;

        void recover();
        void apply(const LogRecord &operation);
        void commitOperation(const LogRecord &operation);
        void ensureReady() const;
    };

}