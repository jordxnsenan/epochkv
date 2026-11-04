#!/bin/sh

set -eu

backup_executable="$1"
test_client="$2"

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

start_backup() {
    "$backup_executable" "$test_port" "$test_directory/backup.log" \
        > "$test_directory/backup.out" 2> "$test_directory/backup.err" &

    backup_pid=$!

    attempts=0

    until "$test_client" "$test_port" probe > /dev/null 2>&1; do
        if ! kill -0 "$backup_pid" 2> /dev/null; 
        
        then
            cat "$test_directory/backup.err" >&2
            exit 1
        fi

        attempts=$((attempts + 1))

        if [ "$attempts" -ge 50 ]; 
        
        then
            echo "Backup did not start" >&2
            exit 1
        fi

        sleep 0.1
    done
}

check_response() {
    expected_response="$1"
    shift

    if ! actual_response="$("$test_client" "$test_port" "$@")"; 
    
    then
        echo "Replication request failed: $*" >&2
        exit 1
    fi

    if [ "$actual_response" != "$expected_response" ]; 
    
    then
        echo "Unexpected response for: $*" >&2
        echo "Expected: $expected_response" >&2
        echo "Actual: $actual_response" >&2
        exit 1
    fi
}

start_backup

check_response "PREPARED" put 1 user:1 Alice

stop_backup
start_backup

check_response "PREPARED" put 1 user:1 Alice
check_response "COMMITTED" commit 1

stop_backup
start_backup

check_response "COMMITTED" commit 1
check_response "PREPARED" delete 2 user:1

stop_backup
start_backup

check_response "PREPARED" delete 2 user:1
check_response "COMMITTED" commit 2

if "$test_client" "$test_port" commit 3 > "$test_directory/error.out" 2>&1; then
    echo "Invalid commit was accepted" >&2
    exit 1
fi

if ! grep -q "ERROR No matching prepared operation" "$test_directory/error.out"; then
    echo "Invalid commit did not return the expected error" >&2
    exit 1
fi

echo "Backup network tests passed"