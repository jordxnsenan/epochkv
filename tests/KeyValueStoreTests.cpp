#include "epochkv/storage/KeyValueStore.hpp"

#include <cstdlib>
#include <iostream>

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main()
{
    epochkv::KeyValueStore store;

    require(!store.get("user:1").has_value(), "missing keys return no value");

    store.put("user:1", "Alice");
    require(store.get("user:1") == "Alice", "put stores a new value");

    store.put("user:1", "Bob");
    require(store.get("user:1") == "Bob", "put replaces an existing value");

    store.put("empty", "");
    require(store.get("empty").has_value(), "an empty value is not a missing key");

    require(store.erase("user:1"), "erase removes an existing key");
    require(!store.get("user:1").has_value(), "deleted keys are missing");
    require(!store.erase("user:1"), "erase reports a missing key");

    std::cout << "KeyValueStore tests passed\n";
}