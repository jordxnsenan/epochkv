#include "epochkv/storage/KeyValueStore.hpp"

namespace epochkv
{

    void KeyValueStore::put(const std::string &key, const std::string &value)
    {
        values_[key] = value;
    }

    std::optional<std::string> KeyValueStore::get(const std::string &key) const
    {
        auto entry = values_.find(key);

        if (entry == values_.end())
        {
            return std::nullopt;
        }

        return entry->second;
    }

    bool KeyValueStore::erase(const std::string &key)
    {
        return values_.erase(key) > 0;
    }

}