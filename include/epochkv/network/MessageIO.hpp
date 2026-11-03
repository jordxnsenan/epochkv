#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace epochkv
{

    constexpr std::size_t MAX_MESSAGE_BYTES = 8192;

    void sendMessage(int socket_descriptor, std::string_view message);
    std::optional<std::string> receiveMessage(int socket_descriptor);

}
