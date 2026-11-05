#!/bin/sh

set -eu

backup_executable="$1"
replicator_tests="$2"
protocol_client="$3"

test_directory="$(mktemp -d)"
test_port="$((35000 + $$ % 20000))"
backup_pid=""

stop_backup() {
    if [ -n "$backup_pid" ]; 
    
    then
        kill "$backup_pid" 2> /dev/null || true
        wait "$backup_pid" 2> /dev/null || true
        backup_pid=""
    fi
}

cleanup() {
    stop_backup

    rm -rf "$test_directory"
}

trap cleanup EXIT

"$backup_executable" "$test_port" "$test_directory/backup.log" \
    > "$test_directory/backup.out" 2> "$test_directory/backup.err" &

backup_pid=$!
attempts=0

until "$protocol_client" "$test_port" probe > /dev/null 2>&1; 

do
    if ! kill -0 "$backup_pid" 2> /dev/null; 
    
    then
        cat "$test_directory/backup.err" >&2
        exit 1
    fi

    attempts=$((attempts + 1))

    if [ "$attempts" -ge 50 ]; 
    
    then
        echo "Backup did not become ready" >&2
        exit 1
    fi

    sleep 0.1
done

"$replicator_tests" \
    "$test_port" \
    "$test_directory/primary-success.log" \
    "$test_directory/backup.log" \
    success

stop_backup

"$replicator_tests" \
    "$test_port" \
    "$test_directory/primary-failure.log" \
    "$test_directory/backup.log" \
    failure

echo "Primary replication integration tests passed"
