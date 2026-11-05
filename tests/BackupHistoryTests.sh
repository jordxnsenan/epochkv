#!/bin/sh

set -eu

backup_executable="$1"
history_test="$2"
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

start_backup() {
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
}

check_history() {
    "$history_test" "$test_port" "$1"
}

check_response() {
    expected_response="$1"

    shift

    actual_response="$("$protocol_client" "$test_port" "$@")"

    if [ "$actual_response" != "$expected_response" ]; 
    
    then
        echo "Unexpected replication response: $actual_response" >&2
        exit 1
    fi
}

start_backup

check_history 0

check_response "PREPARED" put 1 user:1 Alice
check_history 1

stop_backup
start_backup

check_history 1

check_response "COMMITTED" commit 1
check_history 2

check_response "PREPARED" delete 2 user:1
check_history 3

stop_backup
start_backup

check_history 3

check_response "COMMITTED" commit 2
check_history 4

echo "Backup history tests passed"
