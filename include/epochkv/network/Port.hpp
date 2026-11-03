#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace epochkv
{

    inline std::uint16_t parsePort(const std::string &port_text)
    {
        constexpr unsigned long MIN_PORT = 1;
        constexpr unsigned long MAX_PORT = 65535;

        std::size_t characters_read = 0;

        unsigned long port_number = std::stoul(port_text, &characters_read);

        if (characters_read != port_text.size() ||
            port_number < MIN_PORT || 
            port_number > MAX_PORT)
        {
            throw std::invalid_argument("Port must be between 1 and 65535");
        }

        return static_cast<std::uint16_t>(port_number);
    }

}