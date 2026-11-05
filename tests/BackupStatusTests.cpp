#include "epochkv/network/Port.hpp"
#include "epochkv/replication/BackupClient.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

int main(int argument_count, char *arguments[])
{
    if (argument_count != 4)
    {
        std::cerr << "Usage: backup_status_tests "
                  << "<port> <expected-committed> <expected-pending>\n";

        return EXIT_FAILURE;
    }

    try
    {
        std::uint16_t port = epochkv::parsePort(arguments[1]);

        std::uint64_t expected_committed = std::stoull(arguments[2]);
        std::uint64_t expected_pending = std::stoull(arguments[3]);

        epochkv::BackupClient backup_client(port);
        epochkv::BackupStatus backup_status = backup_client.status();

        if (backup_status.last_committed_sequence != expected_committed ||
            backup_status.pending_sequence != expected_pending)
        {
            std::cerr << "Unexpected backup status\n"
                      << "Committed: " << backup_status.last_committed_sequence
                      << ", Pending: " << backup_status.pending_sequence << '\n';

            return EXIT_FAILURE;
        }

        std::cout << "Backup status verified\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "Test failed: " << error.what() << '\n';

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
