#include "epochkv/network/Port.hpp"
#include "epochkv/replication/BackupClient.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

bool sameRecord(const epochkv::LogRecord &first, const epochkv::LogRecord &second)
{
    return first.type == second.type &&
           first.sequence_number == second.sequence_number &&
           first.key == second.key &&
           first.value == second.value;
}

int main(int argument_count, char *arguments[])
{
    if (argument_count != 3)
    {
        std::cerr << "Usage: backup_history_tests <port> <expected-count>\n";

        return EXIT_FAILURE;
    }

    try
    {
        std::uint16_t port = epochkv::parsePort(arguments[1]);
        std::size_t expected_count = std::stoul(arguments[2]);

        const std::vector<epochkv::LogRecord> expected_records{
            {epochkv::LogRecordType::Put, 1, "user:1", "Alice"},
            {epochkv::LogRecordType::Commit, 1, "", ""},
            {epochkv::LogRecordType::Delete, 2, "user:1", ""},
            {epochkv::LogRecordType::Commit, 2, "", ""}};

        if (expected_count > expected_records.size())
        {
            throw std::invalid_argument("Invalid expected record count");
        }

        epochkv::BackupClient backup_client(port);
        std::vector<epochkv::LogRecord> records = backup_client.history();

        if (records.size() != expected_count)
        {
            throw std::runtime_error("Backup returned an unexpected number of records");
        }

        for (std::size_t index = 0; index < records.size(); ++index)
        {
            if (!sameRecord(records[index], expected_records[index]))
            {
                throw std::runtime_error("Backup history contains an unexpected record");
            }
        }

        std::cout << "Backup history verified: "
                  << records.size() << " records\n";
    }

    catch (const std::exception &error)
    {
        std::cerr << "Test failed: " << error.what() << '\n';

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
