#include "epochkv/network/Port.hpp"
#include "epochkv/replication/BackupClient.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main(int argument_count, char *arguments[])
{
    if (argument_count != 2)
    {
        std::cerr << "Usage: backup_client_tests <port>\n";

        return EXIT_FAILURE;
    }

    try
    {
        std::signal(SIGPIPE, SIG_IGN);

        epochkv::BackupClient backup(epochkv::parsePort(arguments[1]));

        epochkv::LogRecord first_write{
            epochkv::LogRecordType::Put, 1, "user:1", "Alice"};

        backup.prepare(first_write);
        backup.prepare(first_write);

        backup.commit(1);
        backup.commit(1);

        bool invalid_commit_rejected = false;

        try
        {
            backup.commit(2);
        }

        catch (const std::runtime_error &)
        {
            invalid_commit_rejected = true;
        }

        if (!invalid_commit_rejected)
        {
            throw std::runtime_error("Backup accepted a commit without a prepared operation");
        }

        backup.prepare({epochkv::LogRecordType::Put, 2, "user:2", "Bob"});

        std::cout << "BackupClient tests passed\n";
    }

    catch (const std::exception &error)
    {
        std::cerr << "Test failed: " << error.what() << '\n';

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
