#!/bin/sh

set -eu

primary_server="$1"
backup_server="$2"
client="$3"
backup_probe="$4"

test_directory="$(mktemp -d)"
primary_port="$((32000 + $$ % 14000))"
backup_port="$((primary_port + 1))"
primary_pid=""
backup_pid=""

stop_primary() {
    if [ -n "$primary_pid" ]; 
    
    then
        kill "$primary_pid" 2> /dev/null || true
        wait "$primary_pid" 2> /dev/null || true
        primary_pid=""
    fi
}

stop_backup() {
    if [ -n "$backup_pid" ]; 
    
    then
        kill "$backup_pid" 2> /dev/null || true
        wait "$backup_pid" 2> /dev/null || true
        backup_pid=""
    fi
}

crash_primary()
{
    if [ -n "$primary_pid" ]; 
    
    then
        kill -9 "$primary_pid" 2>/dev/null || true
        wait "$primary_pid" 2>/dev/null || true
        primary_pid=""
    fi
}

crash_backup()
{
    if [ -n "$backup_pid" ]; 
    
    then
        kill -9 "$backup_pid" 2>/dev/null || true
        wait "$backup_pid" 2>/dev/null || true
        backup_pid=""
    fi
}

cleanup() {
    stop_primary
    stop_backup

    rm -rf "$test_directory"
}

trap cleanup EXIT

start_backup() {
    "$backup_server" "$backup_port" "$test_directory/backup.log" \
        > "$test_directory/backup.out" 2> "$test_directory/backup.err" &

    backup_pid=$!
    attempts=0

    until "$backup_probe" "$backup_port" probe > /dev/null 2>&1; 
    
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

start_primary() {
    "$primary_server" "$primary_port" "$test_directory/primary.log" \
        --backup-port "$backup_port" \
        > "$test_directory/primary.out" 2> "$test_directory/primary.err" &

    primary_pid=$!
    attempts=0

    until "$client" "$primary_port" get __probe__ > /dev/null 2>&1; 
    
    do
        if ! kill -0 "$primary_pid" 2> /dev/null; 
        
        then
            cat "$test_directory/primary.err" >&2
            exit 1
        fi

        attempts=$((attempts + 1))

        if [ "$attempts" -ge 50 ]; 
        
        then
            echo "Primary did not become ready" >&2
            exit 1
        fi

        sleep 0.1
    done
}

check_output() {
    expected="$1"
    shift

    actual="$("$client" "$primary_port" "$@")"

    if [ "$actual" != "$expected" ]; 
    
    then
        echo "Unexpected result for: $*" >&2
        echo "Expected: $expected" >&2
        echo "Actual: $actual" >&2
        exit 1
    fi
}

check_logs_match() {
    if ! cmp -s "$test_directory/primary.log" "$test_directory/backup.log"; 
    
    then
        echo "Primary and backup logs differ" >&2
        exit 1
    fi
}

start_backup
start_primary

check_output "OK" put user:1 Alice
check_output "OK" put user:2 "Bob Smith"
check_output "OK" delete user:1
check_output "VALUE Bob Smith" get user:2
check_output "NOT_FOUND" get user:1

stop_primary
stop_backup
check_logs_match

start_backup
start_primary

check_output "VALUE Bob Smith" get user:2
check_output "OK" put user:3 Carol

crash_backup


if "$client" "$primary_port" put pending eventual \
    > "$test_directory/failure.out" 2>&1; 
    
then
    echo "Write succeeded while backup was unavailable" >&2
    exit 1
fi

if "$client" "$primary_port" get user:2 > /dev/null 2>&1; 

then
    echo "Primary served a read before reconciliation" >&2
    exit 1
fi

crash_primary
start_backup
start_primary


check_output "VALUE Carol" get user:3
check_output "VALUE eventual" get pending
check_output "NOT_FOUND" get user:1
check_output "OK" put user:4 Dana

# acknowledges writes must survive abrupt process termination
crash_primary
crash_backup
check_logs_match


echo "Replicated server tests passed"