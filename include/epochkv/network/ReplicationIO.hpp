#pragma once

#include "epochkv/storage/LogRecord.hpp"

#include <optional>

namespace epochkv
{

    void sendReplicationRecord(int socket_descriptor, const LogRecord &record);

    std::optional<LogRecord> receiveReplicationRecord(int socket_descriptor);

}