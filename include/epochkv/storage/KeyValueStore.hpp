#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace epochkv
{

    class KeyValueStore
    {
    public:
        void put(const std::string &key, const std::string &value);
        std::optional<std::string> get(const std::string &key) const;
        bool erase(const std::string &key);

    private:
        std::unordered_map<std::string, std::string> values_;
    };

}