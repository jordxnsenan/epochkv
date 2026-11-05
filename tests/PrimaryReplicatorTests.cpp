#include "epochkv/Database.hpp"
#include "epochkv/network/Port.hpp"
#include "epochkv/replication/BackupClient.hpp"
#include "epochkv/replication/PrimaryReplicator.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';

        std::exit(EXIT_FAILURE);
    }
}

void testSuccessfulReplication(
    std::uint16_t backup_port,
    const std::filesystem::path &primary_log,
    const std::filesystem::path &backup_log)
{
    epochkv::Database primary(primary_log);
    epochkv::BackupClient backup_client(backup_port);
    epochkv::PrimaryReplicator replicator(primary, backup_client);

    replicator.put("user:1", "Alice");
    replicator.put("user:2", "Bob");

    require(replicator.get("user:1") == "Alice",
            "primary stores replicated writes");

    require(replicator.erase("user:1"),
            "existing key can be deleted");

    require(!replicator.erase("missing"),
            "missing key creates no operation");

    require(!replicator.get("user:1").has_value(),
            "deleted key is no longer visible");

    require(primary.lastCommittedSequence() == 3,
            "primary commits three operations");

    epochkv::Database backup_state(backup_log);

    require(!backup_state.get("user:1").has_value(),
            "backup contains the replicated deletion");

    require(backup_state.get("user:2") == "Bob",
            "backup contains the replicated value");

    require(backup_state.lastCommittedSequence() == 3,
            "backup has the same committed sequence");

    // ensures restarted databases are synchronized before accepting new writes
    epochkv::Database restarted_primary(primary_log);
    epochkv::PrimaryReplicator restarted_replicator(restarted_primary, backup_client);

    bool rejected = false;

    try
    {
        restarted_replicator.put("user:3", "Charlie");
    }

    catch (const std::runtime_error &)
    {
        rejected = true;
    }

    require(rejected, "restart requires reconciliation");
}

void testUnavailableBackup(
    std::uint16_t backup_port,
    const std::filesystem::path &primary_log)
{
    epochkv::Database primary(primary_log);
    epochkv::BackupClient backup_client(backup_port);
    epochkv::PrimaryReplicator replicator(primary, backup_client);

    bool failed = false;

    try
    {
        replicator.put("unfinished", "hidden");
    }

    catch (const std::exception &)
    {
        failed = true;
    }

    require(failed, "write fails when backup is unavailable");
    require(primary.pendingOperation().has_value(), "primary retains its prepared operation");

    require(!primary.get("unfinished").has_value(), "uncommitted value is not applied locally");

    bool blocked = false;

    try
    {
        replicator.put("another", "value");
    }

    catch (const std::runtime_error &)
    {
        blocked = true;
    }

    require(blocked, "further writes require reconciliation");

    epochkv::Database recovered_primary(primary_log);

    require(recovered_primary.pendingOperation().has_value(), "unfinished operation survives restart");
}

int main(int argument_count, char *arguments[])
{
    if (argument_count != 5)
    {
        std::cerr << "Usage: primary_replicator_tests "
                  << "<port> <primary-log> <backup-log> <success|failure>\n";

        return EXIT_FAILURE;
    }

    try
    {
        std::uint16_t backup_port = epochkv::parsePort(arguments[1]);
        std::string test_mode = arguments[4];

        if (test_mode == "success")
        {
            testSuccessfulReplication(backup_port, arguments[2], arguments[3]);
        }

        else if (test_mode == "failure")
        {
            testUnavailableBackup(backup_port, arguments[2]);
        }

        else
        {
            throw std::invalid_argument("Unknown test mode");
        }

        std::cout << "PrimaryReplicator tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "Test failed: " << error.what() << '\n';

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
