#pragma once

#include "epochkv/storage/KeyValueStore.hpp"
#include "epochkv/storage/LogStore.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace epochkv
{

    class Database
    {
    public:
        explicit Database(const std::filesystem::path &log_path);

        void put(const std::string &key, const std::string &value);
        std::optional<std::string> get(const std::string &key) const;
        bool erase(const std::string &key);

        std::uint64_t preparePut(const std::string &key, const std::string &value);
        std::optional<std::uint64_t> prepareDelete(const std::string &key);
        void commitPrepared(std::uint64_t sequence_number);

        std::optional<LogRecord> pendingOperation() const;
        std::uint64_t lastCommittedSequence() const;

        std::vector<LogRecord> history();

    private:
        LogStore log_store_;
        KeyValueStore key_value_store_;

        std::uint64_t next_sequence_number_ = 1;
        std::uint64_t last_committed_sequence_ = 0;
        std::optional<LogRecord> pending_operation_;
        bool requires_restart_ = false;

        void recover();
        void apply(const LogRecord &operation);
        std::uint64_t prepareOperation(const LogRecord &operation);
        void ensureReady() const;
    };

}