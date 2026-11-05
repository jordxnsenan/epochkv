#pragma once

#include "epochkv/storage/LogRecord.hpp"

#include <optional>
#include <string>

namespace epochkv
{

    void sendReplicationRecord(int socket_descriptor, const LogRecord &record);

    std::optional<LogRecord> receiveReplicationRecord(int socket_descriptor);

    LogRecord receiveReplicationRecord(int socket_descriptor, const std::string &type_text);

}