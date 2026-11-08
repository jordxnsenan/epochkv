#include "epochkv/Database.hpp"
#include "epochkv/network/Port.hpp"
#include "epochkv/replication/BackupClient.hpp"
#include "epochkv/replication/LogHistory.hpp"
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

void requireSynchronized(epochkv::Database &primary,
                         const epochkv::BackupClient &backup)
{
    auto primary_history = primary.history();
    auto backup_history = backup.history();

    epochkv::verifyMatchingHistory(primary_history, backup_history);

    require(primary_history.size() == backup_history.size(), "primary and backup histories are identical");
}

void testCatchup(std::uint16_t port, const std::filesystem::path &log_path)
{
    epochkv::Database primary(log_path);

    primary.put("user:1", "Alice");
    primary.put("user:2", "Bob");

    epochkv::BackupClient backup(port);
    epochkv::PrimaryReplicator replicator(primary, backup);

    replicator.reconcile();
    requireSynchronized(primary, backup);

    replicator.put("user:3", "Charlie");

    require(replicator.get("user:3") == "Charlie", "new writes succeed after reconciliation");

    requireSynchronized(primary, backup);
}

void testRestart(std::uint16_t port, const std::filesystem::path &log_path)
{
    epochkv::Database primary(log_path);
    epochkv::BackupClient backup(port);
    epochkv::PrimaryReplicator replicator(primary, backup);

    replicator.reconcile();

    require(replicator.get("user:3") == "Charlie",
            "committed data survives a restart");

    require(replicator.erase("user:1"),
            "replicated deletion succeeds");

    requireSynchronized(primary, backup);
}

void testPendingWrite(std::uint16_t port, const std::filesystem::path &log_path)
{
    epochkv::Database primary(log_path);

    primary.preparePut("pending", "saved");

    epochkv::BackupClient backup(port);
    epochkv::PrimaryReplicator replicator(primary, backup);

    replicator.reconcile();

    require(primary.get("pending") == "saved", "pending write is committed during reconciliation");

    require(!primary.pendingOperation().has_value(), "reconciliation clears the pending operation");

    requireSynchronized(primary, backup);
}

void testConflictingHistory(std::uint16_t port, const std::filesystem::path &log_path)
{
    epochkv::Database primary(log_path);
    primary.put("user:1", "Alice");

    epochkv::BackupClient backup(port);
    epochkv::PrimaryReplicator replicator(primary, backup);

    bool rejected = false;

    try
    {
        replicator.reconcile();
    }

    catch (const std::runtime_error &)
    {
        rejected = true;
    }

    require(rejected, "conflicting histories are rejected");
    require(primary.get("user:1") == "Alice", "failed reconciliation does not modify committed data");
}

void testBackupAhead(std::uint16_t port, const std::filesystem::path &log_path)
{
    epochkv::Database primary(log_path);
    epochkv::BackupClient backup(port);
    epochkv::PrimaryReplicator replicator(primary, backup);

    bool rejected = false;

    try
    {
        replicator.reconcile();
    }

    catch (const std::runtime_error &)
    {
        rejected = true;
    }

    require(rejected, "backup ahead of the primary is rejected");
}

int main(int argument_count, char *arguments[])
{
    if (argument_count != 4)
    {
        std::cerr << "Usage: reconciliation_tests <port> <primary-log> <mode>\n";

        return EXIT_FAILURE;
    }

    try
    {
        std::uint16_t port = epochkv::parsePort(arguments[1]);
        std::filesystem::path log_path = arguments[2];
        std::string mode = arguments[3];

        if (mode == "catchup")
            testCatchup(port, log_path);

        else if (mode == "restart")
            testRestart(port, log_path);

        else if (mode == "pending")
            testPendingWrite(port, log_path);

        else if (mode == "conflict")
            testConflictingHistory(port, log_path);

        else if (mode == "ahead")
            testBackupAhead(port, log_path);

        else
            throw std::invalid_argument("Unknown reconciliation test mode");

        std::cout << "Reconciliation test passed: " << mode << '\n';
    }

    catch (const std::exception &error)
    {
        std::cerr << "Test failed: " << error.what() << '\n';

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
