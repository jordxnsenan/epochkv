#include "epochkv/Database.hpp"
#include "epochkv/storage/LogStore.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';

        std::exit(EXIT_FAILURE);
    }
}

void testCommittedDataSurvivesRestart(const fs::path &log_path)
{
    fs::remove(log_path);

    {
        epochkv::Database database(log_path);

        database.put("user:1", "Alice");
        database.put("user:1", "Bob");
        database.put("empty", "");

        require(database.erase("user:1"), "existing key can be deleted");
        require(!database.erase("missing"), "missing key cannot be deleted");
    }

    {
        epochkv::Database database(log_path);

        require(!database.get("user:1").has_value(), "deleted key stays deleted");
        require(database.get("empty") == "", "empty value survives restart");

        database.put("after:restart", "saved");
    }

    {
        epochkv::Database database(log_path);

        require(database.get("after:restart") == "saved",
                "new writes survive another restart");
    }

    fs::remove(log_path);
}

void testPreparedWrite(const fs::path &log_path)
{
    fs::remove(log_path);

    epochkv::Database database(log_path);

    std::uint64_t sequence_number = database.preparePut("user:1", "Alice");

    require(sequence_number == 1, "first operation gets sequence 1");
    require(!database.get("user:1").has_value(), "prepared write is not visible");
    require(database.pendingOperation().has_value(), "database tracks pending write");

    bool rejected = false;

    try
    {
        database.preparePut("user:2", "Bob");
    }

    catch (const std::logic_error &)
    {
        rejected = true;
    }

    require(rejected, "another write cannot overtake a pending operation");

    database.commitPrepared(sequence_number);

    require(database.get("user:1") == "Alice", "committed write becomes visible");
    require(!database.pendingOperation().has_value(), "commit clears pending operation");
    require(database.lastCommittedSequence() == 1, "commit sequence is tracked");

    fs::remove(log_path);
}

void testPreparedDelete(const fs::path &log_path)
{
    fs::remove(log_path);

    epochkv::Database database(log_path);

    database.put("user:1", "Alice");

    auto sequence_number = database.prepareDelete("user:1");

    require(sequence_number.has_value(), "existing key can be prepared for deletion");
    require(database.get("user:1") == "Alice", "key remains visible before commit");

    database.commitPrepared(*sequence_number);

    require(!database.get("user:1").has_value(), "committed deletion removes key");
    require(!database.prepareDelete("missing").has_value(),
            "deleting a missing key creates no operation");

    fs::remove(log_path);
}

void testPendingOperationSurvivesRestart(const fs::path &log_path)
{
    fs::remove(log_path);

    {
        epochkv::Database database(log_path);

        database.preparePut("unfinished", "hidden");
    }

    {
        epochkv::Database database(log_path);

        require(!database.get("unfinished").has_value(),
                "uncommitted write remains invisible after restart");

        auto pending_operation = database.pendingOperation();

        require(pending_operation.has_value(), "pending operation is recovered");
        require(pending_operation->sequence_number == 1,
                "pending sequence number is preserved");

        bool rejected = false;

        try
        {
            database.put("new", "value");
        }

        catch (const std::logic_error &)
        {
            rejected = true;
        }

        require(rejected, "new writes wait for pending operation to be resolved");

        database.commitPrepared(1);
    }

    {
        epochkv::Database database(log_path);

        require(database.get("unfinished") == "hidden",
                "resolved operation survives restart");

        database.put("new", "value");

        require(database.lastCommittedSequence() == 2,
                "sequence numbers continue after recovery");
    }

    fs::remove(log_path);
}

void testInvalidCommit(const fs::path &log_path)
{
    fs::remove(log_path);

    epochkv::LogStore log_store(log_path);
    log_store.append({epochkv::LogRecordType::Commit, 1, "", ""});

    bool rejected = false;

    try
    {
        epochkv::Database database(log_path);
    }

    catch (const std::runtime_error &)
    {
        rejected = true;
    }

    require(rejected, "commit without preparation is rejected");

    fs::remove(log_path);
}

void testInvalidInputDoesNotBlockDatabase(const fs::path &log_path)
{
    fs::remove(log_path);

    epochkv::Database database(log_path);

    bool rejected = false;

    try
    {
        database.put("", "invalid");
    }

    catch (const std::invalid_argument &)
    {
        rejected = true;
    }

    require(rejected, "empty key is rejected");

    database.put("valid", "Alice");

    require(database.get("valid") == "Alice",
            "database still accepts writes after invalid input");

    fs::remove(log_path);
}

int main()
{
    fs::path test_directory = fs::temp_directory_path();

    std::string prefix = "epochkv-database-test-" + std::to_string(::getpid());

    testCommittedDataSurvivesRestart(test_directory / (prefix + "-restart.log"));
    testPreparedWrite(test_directory / (prefix + "-prepare.log"));
    testPreparedDelete(test_directory / (prefix + "-delete.log"));
    testPendingOperationSurvivesRestart(test_directory / (prefix + "-pending.log"));
    testInvalidCommit(test_directory / (prefix + "-invalid.log"));
    testInvalidInputDoesNotBlockDatabase(test_directory / (prefix + "-input.log"));

    std::cout << "Database tests passed\n";
}
