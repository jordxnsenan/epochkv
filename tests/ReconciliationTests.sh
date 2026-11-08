#!/bin/sh

set -eu

backup_executable="$1"
reconciliation_tests="$2"
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
    backup_log="$1"

    "$backup_executable" "$test_port" "$backup_log" \
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

run_test() {
    "$reconciliation_tests" "$test_port" "$test_directory/$1.log" "$2"
}

# catches up an empty backup then restart both database instances
start_backup "$test_directory/catchup-backup.log"
run_test catchup-primary catchup

stop_backup
start_backup "$test_directory/catchup-backup.log"
run_test catchup-primary restart
stop_backup

# recovers a primary write that never reached the backup
start_backup "$test_directory/pending-backup.log"
run_test pending-primary pending
stop_backup

# recovers a write that the backup has already prepared
start_backup "$test_directory/prepared-backup.log"
"$protocol_client" "$test_port" put 1 pending saved
run_test prepared-primary pending
stop_backup

# Rejects different operations stored at the same sequence
start_backup "$test_directory/conflict-backup.log"
"$protocol_client" "$test_port" put 1 user:1 Bob
"$protocol_client" "$test_port" commit 1
run_test conflict-primary conflict
stop_backup

# rejects a backup containing operations absent from the primary
start_backup "$test_directory/ahead-backup.log"
"$protocol_client" "$test_port" put 1 user:1 Alice
"$protocol_client" "$test_port" commit 1
run_test ahead-primary ahead
stop_backup

echo "Reconciliation integration tests passed"