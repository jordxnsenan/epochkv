#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

BENCHMARK_REQUESTS=1000
BENCHMARK_TRIALS=5

PRIMARY_PORT=$((32000 + $$ % 20000))
BACKUP_PORT=$((PRIMARY_PORT + 1))

PROJECT_DIRECTORY="$(pwd)"

PRIMARY_SERVER="$PROJECT_DIRECTORY/build/epochkv_server"
BACKUP_SERVER="$PROJECT_DIRECTORY/build/epochkv_backup"
CLIENT="$PROJECT_DIRECTORY/build/epochkv_client"
BACKUP_PROBE="$PROJECT_DIRECTORY/build/backup_protocol_client"
BENCHMARK="$PROJECT_DIRECTORY/build/epochkv_benchmark"

RESULTS_FILE="$PROJECT_DIRECTORY/data/benchmark-results.csv"
TEST_DIRECTORY="$(mktemp -d)"

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

cleanup() {
    stop_primary
    stop_backup
    rm -rf "$TEST_DIRECTORY"
}

trap cleanup EXIT

for executable in "$PRIMARY_SERVER" "$BACKUP_SERVER" "$CLIENT" "$BACKUP_PROBE" "$BENCHMARK"; do
    if [ ! -x "$executable" ]; 
    
    then
        echo "Missing executable: $executable" >&2
        exit 1
    fi
done

start_backup() {
    log_prefix="$1"

    "$BACKUP_SERVER" "$BACKUP_PORT" "${log_prefix}-backup.log" \
        > "${log_prefix}-backup.out" 2> "${log_prefix}-backup.err" &

    backup_pid=$!
    attempts=0

    until "$BACKUP_PROBE" "$BACKUP_PORT" probe > /dev/null 2>&1; 
    
    do
        if ! kill -0 "$backup_pid" 2> /dev/null; 
        
        then
            cat "${log_prefix}-backup.err" >&2
            exit 1
        fi

        attempts=$((attempts + 1))

        if [ "$attempts" -ge 100 ]; 
        
        then
            echo "Backup did not become ready" >&2
            exit 1
        fi

        sleep 0.1
    done
}

start_primary() {
    server_mode="$1"
    log_prefix="$2"

    if [ "$server_mode" = "replicated" ]; 
    
    then
        "$PRIMARY_SERVER" "$PRIMARY_PORT" "${log_prefix}-primary.log" \
            --backup-port "$BACKUP_PORT" \
            > "${log_prefix}-primary.out" 2> "${log_prefix}-primary.err" &
    else
        "$PRIMARY_SERVER" "$PRIMARY_PORT" "${log_prefix}-primary.log" \
            > "${log_prefix}-primary.out" 2> "${log_prefix}-primary.err" &
    fi

    primary_pid=$!
    attempts=0

    until "$CLIENT" "$PRIMARY_PORT" get __benchmark_probe__ > /dev/null 2>&1; 
    
    do
        if ! kill -0 "$primary_pid" 2> /dev/null; 
        
        then
            cat "${log_prefix}-primary.err" >&2
            exit 1
        fi

        attempts=$((attempts + 1))

        if [ "$attempts" -ge 100 ]; 
        
        then
            echo "Primary did not become ready" >&2
            exit 1
        fi

        sleep 0.1
    done
}

run_trial() {
    trial_number="$1"
    mode="$2"
    trial_prefix="$TEST_DIRECTORY/${mode}-${trial_number}"

    if [ "$mode" = "replicated" ]; 
    
    then
        start_backup "$trial_prefix"
    fi

    start_primary "$mode" "$trial_prefix"

    for operation in put get; 
    
    do
        result_row="$("$BENCHMARK" "$PRIMARY_PORT" "$operation" "$BENCHMARK_REQUESTS" --csv)"

        printf '%s,%s,%s\n' \
            "$trial_number" "$mode" "$result_row" >> "$RESULTS_FILE"
    done

    stop_primary
    stop_backup

    echo "Finished $mode trial $trial_number"
}

mkdir -p "$PROJECT_DIRECTORY/data"

printf '%s\n' \
    'trial,mode,operation,requests,value_bytes,throughput_rps,p50_ms,p95_ms,p99_ms' \
    > "$RESULTS_FILE"

trial_number=1

while [ "$trial_number" -le "$BENCHMARK_TRIALS" ]; 

do
    # alternates the order so one config. doesn't always run first
    if [ $((trial_number % 2)) -eq 1 ]; 
    
    then
        run_trial "$trial_number" standalone
        run_trial "$trial_number" replicated
    else
        run_trial "$trial_number" replicated
        run_trial "$trial_number" standalone
    fi

    trial_number=$((trial_number + 1))
done

echo "Benchmark results saved to $RESULTS_FILE"
