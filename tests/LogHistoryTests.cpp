#include "epochkv/replication/LogHistory.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';

        std::exit(EXIT_FAILURE);
    }
}

void expectHistoryMismatch(
    const std::vector<epochkv::LogRecord> &primary_history,
    const std::vector<epochkv::LogRecord> &backup_history,
    const char *message)
{
    bool rejected = false;

    try
    {
        epochkv::verifyMatchingHistory(primary_history, backup_history);
    }

    catch (const std::runtime_error &)
    {
        rejected = true;
    }

    require(rejected, message);
}

int main()
{
    using epochkv::LogRecord;
    using epochkv::LogRecordType;

    const std::vector<LogRecord> primary_history{
        {LogRecordType::Put, 1, "user:1", "Alice"},
        {LogRecordType::Commit, 1, "", ""},
        {LogRecordType::Put, 2, "user:2", "Bob"},
        {LogRecordType::Commit, 2, "", ""}};

    // ensures empty or partially synchronized backup is allowed
    epochkv::verifyMatchingHistory(primary_history, {});
    epochkv::verifyMatchingHistory(primary_history, {primary_history[0]});

    epochkv::verifyMatchingHistory(
        primary_history,
        {primary_history[0], primary_history[1]});

    epochkv::verifyMatchingHistory(primary_history, primary_history);
    epochkv::verifyMatchingHistory({}, {});

    expectHistoryMismatch(
        primary_history,
        {{LogRecordType::Put, 1, "user:1", "Different"}},
        "different values are rejected");

    expectHistoryMismatch(
        primary_history,
        {{LogRecordType::Delete, 1, "user:1", ""}},
        "different operation types are rejected");

    expectHistoryMismatch(
        primary_history,
        {{LogRecordType::Put, 5, "user:1", "Alice"}},
        "different sequence numbers are rejected");

    std::vector<LogRecord> backup_ahead = primary_history;

    backup_ahead.push_back(
        {LogRecordType::Put, 3, "user:3", "Charlie"});

    expectHistoryMismatch(
        primary_history,
        backup_ahead,
        "backup cannot be ahead of the primary");

    std::cout << "LogHistory tests passed\n";
}
