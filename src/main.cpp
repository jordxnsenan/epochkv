#include "epochkv/Database.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{

    constexpr int PUT_ARGUMENT_COUNT = 4;
    constexpr int KEY_ARGUMENT_COUNT = 3;

    void printUsage(std::ostream &output)
    {
        output << "Usage:\n"
               << "epochkv put <key> <value>\n"
               << "epochkv get <key>\n"
               << "epochkv delete <key>\n";
    }

}

int main(int argument_count, char *arguments[])
{
    if (argument_count < 2)
    {
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }

    std::string command = arguments[1];

    if (command == "--help")
    {
        printUsage(std::cout);
        return EXIT_SUCCESS;
    }

    bool is_put_command = command == "put";
    bool is_key_command = command == "get" || command == "delete";

    bool has_valid_arguments =
        (is_put_command && argument_count == PUT_ARGUMENT_COUNT) ||
        (is_key_command && argument_count == KEY_ARGUMENT_COUNT);

    if (!has_valid_arguments)
    {
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }

    std::string key = arguments[2];

    if (key.empty())
    {
        std::cerr << "Error: key cannot be empty\n";
        return EXIT_FAILURE;
    }

    try
    {
        std::filesystem::path data_directory = "data";
        std::filesystem::create_directories(data_directory);

        std::filesystem::path log_path = data_directory / "epochkv.log";
        epochkv::Database database(log_path);

        if (command == "put")
        {
            database.put(key, arguments[3]);
            std::cout << "OK\n";
        }
        else if (command == "get")
        {
            auto value = database.get(key);

            if (value.has_value())
            {
                std::cout << "VALUE " << *value << '\n';
            }
            else
            {
                std::cout << "NOT_FOUND\n";
            }
        }
        else if (command == "delete")
        {
            bool was_deleted = database.erase(key);
            std::cout << (was_deleted ? "OK" : "NOT_FOUND") << '\n';
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}