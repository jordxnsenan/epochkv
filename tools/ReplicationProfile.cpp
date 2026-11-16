#include "epochkv/Database.hpp"
#include "epochkv/network/Port.hpp"
#include "epochkv/replication/BackupClient.hpp"
#include "epochkv/replication/PrimaryReplicator.hpp"

#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace
{

    constexpr std::size_t WARMUP_WRITES = 20;
    constexpr std::size_t MAX_PROFILE_WRITES = 100000;
    constexpr std::size_t VALUE_BYTES = 128;

    std::size_t parseWriteCount(const std::string &text)
    {
        std::size_t write_count = 0;

        auto result = std::from_chars(
            text.data(),
            text.data() + text.size(),
            write_count);

        if (result.ec != std::errc{} ||
            result.ptr != text.data() + text.size() ||
            write_count == 0 ||
            write_count > MAX_PROFILE_WRITES)
        {
            throw std::invalid_argument("Write count must be between 1 and 100000");
        }

        return write_count;
    }

    void addTiming(
        epochkv::ReplicationWriteTiming &total,
        const epochkv::ReplicationWriteTiming &sample)
    {
        total.primary_prepare_ms += sample.primary_prepare_ms;
        total.backup_prepare_ms += sample.backup_prepare_ms;
        total.primary_commit_ms += sample.primary_commit_ms;
        total.backup_commit_ms += sample.backup_commit_ms;
    }

}

int main(int argument_count, char *arguments[])
{
    if (argument_count != 4)
    {
        std::cerr << "Usage: epochkv_replication_profile "
                  << "<backup-port> <primary-log> <writes>\n";

        return EXIT_FAILURE;
    }

    try
    {
        std::signal(SIGPIPE, SIG_IGN);

        std::uint16_t backup_port = epochkv::parsePort(arguments[1]);
        std::filesystem::path primary_log = arguments[2];
        std::size_t write_count = parseWriteCount(arguments[3]);

        if (primary_log.has_parent_path())
        {
            std::filesystem::create_directories(primary_log.parent_path());
        }

        epochkv::Database database(primary_log);
        epochkv::BackupClient backup_client(backup_port);
        epochkv::PrimaryReplicator replicator(database, backup_client);

        replicator.reconcile();

        std::string value(VALUE_BYTES, 'x');
        epochkv::ReplicationWriteTiming total{};

        for (std::size_t write_index = 0;
             write_index < WARMUP_WRITES + write_count;
             ++write_index)
        {
            replicator.put(
                "profile:key:" + std::to_string(write_index),
                value);

            if (write_index < WARMUP_WRITES)
            {
                continue;
            }

            auto timing = replicator.lastWriteTiming();

            if (!timing)
            {
                throw std::runtime_error("Missing timing for completed write");
            }

            addTiming(total, *timing);
        }

        double average_primary_prepare = total.primary_prepare_ms / write_count;
        double average_backup_prepare = total.backup_prepare_ms / write_count;
        double average_primary_commit = total.primary_commit_ms / write_count;
        double average_backup_commit = total.backup_commit_ms / write_count;

        double average_total = average_primary_prepare +
                               average_backup_prepare +
                               average_primary_commit +
                               average_backup_commit;

        std::cout << std::fixed << std::setprecision(3)
                  << "Replicated write profile ("
                  << write_count << " measured writes)\n"
                  << "Primary prepare: " << average_primary_prepare << " ms\n"
                  << "Backup prepare round-trip: " << average_backup_prepare << " ms\n"
                  << "Primary commit: " << average_primary_commit << " ms\n"
                  << "Backup commit round-trip: " << average_backup_commit << " ms\n"
                  << "Sum of stages: " << average_total << " ms\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "Profile error: " << error.what() << '\n';

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
