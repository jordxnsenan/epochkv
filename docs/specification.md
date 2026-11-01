# EpochKV — Engineering Specification

## Overview

EpochKV is a small replicated key-value database written in C++. It stores records in memory for fast access while maintaining an append-only disk log that can be used to recover data after a process crash.

Two server processes maintain the database. One acts as the primary and accepts client operations. The other runs as a backup, receiving changes over TCP and storing them locally.

The project uses a fixed primary–backup arrangement. There is no leader election or automatic failover.

## Supported Operations

The database exposes a small interface:

Command : Behaviouur
`PUT(key, value)` : Insert a new key or overwrite an existing value
`GET(key)` : Return the stored value or `NOT_FOUND`
`DELETE(key)` : Remove a key if it exists
`STATUS()` : Return basic server and replication information

Keys and values are strings. Keys are limited to 256 bytes and values to 4 KiB in the initial version.

The command-line client communicates with the primary over TCP. Direct client reads and writes on the backup are not supported.

## Storage

Each server maintains an in-memory hash map and its own append-only log file.

The map holds the current committed state. The log contains the information needed to reconstruct that state after restarting the process.

Log records carry a sequence number and an operation. I will distinguish prepared operations from committed ones since a write might reach only one server before communication fails.

Records use a length-prefixed binary format with a checksum. Recovery can discard incomplete trailing records but unexpected corruption must be reported as opposed to being silently ignored.

File operations must be checked for errors. A write is not considered durable until the required disk synchronization succeeds.

## Replication

EpochKV uses synchronous primary–backup replication.

The primary processes one mutation at a time, preserving a consistent order of writes. It records a prepared operation locally before forwarding it to the backup.

The backup persists the prepared operation and acknowledges receipt. The primary can then durably commit the operation and instruct the backup to commit it as well.

A client receives success only after both servers have durably recorded the commit.

The backup does not expose prepared operations as committed database state.

Reads are handled by the primary and return committed values. During recovery or unresolved replication, requests may be delayed or return an availability error.

A timed-out request has an uncertain outcome. It may have committed even if the client never received confirmation. The initial version will not promise exactly-once request execution.

## Crash Recovery

On startup a server reads its log and rebuilds the in-memory map from committed operations.

Prepared operations without a matching commit record remain invisible to reads.

If the backup restarts or loses synchronization, it must recover the primary's committed history before participating in new writes. The first implementation will transfer the complete committed log rather than use incremental replication.

A restarted primary recovers its committed operations from disk and retains its designated role. It does not assume the backup is synchronized until the two processes have completed recovery.

## Failure Behavior

If the primary crashes, the database stops serving client requests until it restarts.

If the backup becomes unavailable, the primary must stop acknowledging new writes. Previously committed data remains stored locally.

After connectivity returns, the servers reconcile their logs before accepting further mutations.

An interrupted operation may have an uncertain outcome. The implementation must preserve committed writes and prevent uncommitted operations from becoming visible during recovery.

The initial system assumes process crashes and temporary network failures. Permanent loss of the primary's disk, arbitrary disk corruption, and automatic promotion of the backup are outside the supported failure model.