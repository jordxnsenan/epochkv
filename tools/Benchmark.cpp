#include "epochkv/network/MessageIO.hpp"
#include "epochkv/network/Port.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

    constexpr std::size_t WARMUP_REQUESTS = 20;
    constexpr std::size_t MAX_BENCHMARK_REQUESTS = 100000;
    constexpr std::size_t BENCHMARK_VALUE_BYTES = 128;

    std::size_t parseRequestCount(const std::string &text)
    {
        std::size_t request_count = 0;

        auto result = std::from_chars(
            text.data(), text.data() + text.size(), request_count);

        if (result.ec != std::errc{} ||
            result.ptr != text.data() + text.size() ||
            request_count == 0 ||
            request_count > MAX_BENCHMARK_REQUESTS)
        {
            throw std::invalid_argument("Invalid request count");
        }

        return request_count;
    }

    int connectToServer(std::uint16_t port)
    {
        int socket_descriptor = ::socket(AF_INET, SOCK_STREAM, 0);

        if (socket_descriptor == -1)
        {
            throw std::system_error(errno, std::generic_category(), "Failed to create socket");
        }

        sockaddr_in server_address{};
        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(port);
        server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::connect(socket_descriptor,
                      reinterpret_cast<sockaddr *>(&server_address),
                      sizeof(server_address)) == -1)
        {
            int error_number = errno;
            ::close(socket_descriptor);

            throw std::system_error(error_number, std::generic_category(), "Failed to connect");
        }

        return socket_descriptor;
    }

    std::string sendRequest(std::uint16_t port, const std::string &command,
                            const std::string &key, const std::string &value)
    {
        int socket_descriptor = connectToServer(port);

        std::string response;

        try
        {
            epochkv::sendMessage(socket_descriptor, command);
            epochkv::sendMessage(socket_descriptor, key);

            if (command == "put")
            {
                epochkv::sendMessage(socket_descriptor, value);
            }

            auto received_response = epochkv::receiveMessage(socket_descriptor);

            if (!received_response)
            {
                throw std::runtime_error("Server disconnected without a response");
            }

            response = *received_response;
        }

        catch (...)
        {
            ::close(socket_descriptor);
            throw;
        }

        ::close(socket_descriptor);

        return response;
    }

    void runRequest(std::uint16_t port, const std::string &command,
                    const std::string &value, std::size_t request_index, bool warmup)
    {
        std::string key = "benchmark:read";

        if (command == "put")
        {
            key = warmup ? "benchmark:warmup:" : "benchmark:write:";
            key += std::to_string(request_index);
        }

        std::string expected_response = (command == "put" ? "OK" : "VALUE " + value);

        std::string actual_response = sendRequest(port, command, key, value);

        if (actual_response != expected_response)
        {
            throw std::runtime_error("Unexpected server response: " + actual_response);
        }
    }

    double percentileLatency(const std::vector<double> &sorted_latencies,
                             double percentile)
    {
        // uses the observation at the rounded-up percentile rank
        std::size_t rank = static_cast<std::size_t>(
            std::ceil((percentile / 100.0) * sorted_latencies.size()));

        return sorted_latencies[rank - 1];
    }

}

int main(int argument_count, char *arguments[])
{
    if (argument_count != 4)
    {
        std::cerr << "Usage: epochkv_benchmark <port> <put|get> <requests>\n";

        return EXIT_FAILURE;
    }

    try
    {
        std::signal(SIGPIPE, SIG_IGN);

        std::uint16_t port = epochkv::parsePort(arguments[1]);
        std::string command = arguments[2];
        std::size_t request_count = parseRequestCount(arguments[3]);

        if (command != "put" && command != "get")
        {
            throw std::invalid_argument("Benchmark command must be put or get");
        }

        std::string value(BENCHMARK_VALUE_BYTES, 'x');

        if (command == "get" &&
            sendRequest(port, "put", "benchmark:read", value) != "OK")
        {
            throw std::runtime_error("Could not prepare the read benchmark");
        }

        for (std::size_t request_index = 0;
             request_index < WARMUP_REQUESTS;
             ++request_index)
        {
            runRequest(port, command, value, request_index, true);
        }

        std::vector<double> latencies_ms;

        latencies_ms.reserve(request_count);

        auto benchmark_start = std::chrono::steady_clock::now();

        for (std::size_t request_index = 0;
             request_index < request_count;
             ++request_index)
        {
            auto request_start = std::chrono::steady_clock::now();

            runRequest(port, command, value, request_index, false);

            auto request_end = std::chrono::steady_clock::now();

            double latency_ms = std::chrono::duration<double, std::milli>(
                                    request_end - request_start)
                                    .count();

            latencies_ms.push_back(latency_ms);
        }

        auto benchmark_end = std::chrono::steady_clock::now();

        double elapsed_seconds = std::chrono::duration<double>(
                                     benchmark_end - benchmark_start)
                                     .count();

        std::sort(latencies_ms.begin(), latencies_ms.end());

        std::cout << std::fixed << std::setprecision(2)
                  << "Operation: " << command << '\n'
                  << "Requests: " << request_count << '\n'
                  << "Value size: " << BENCHMARK_VALUE_BYTES << " bytes\n"
                  << "Throughput: " << request_count / elapsed_seconds
                  << " requests/sec\n"
                  << "p50 latency: " << percentileLatency(latencies_ms, 50.0)
                  << " ms\n"
                  << "p95 latency: " << percentileLatency(latencies_ms, 95.0)
                  << " ms\n"
                  << "p99 latency: " << percentileLatency(latencies_ms, 99.0)
                  << " ms\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "Benchmark error: " << error.what() << '\n';

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
