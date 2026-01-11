#!/bin/bash
# Docker entrypoint script for postfiatd
# Starts cron for log rotation and then runs postfiatd

set -e

# Start cron daemon in background for log rotation
echo "Starting cron daemon for log rotation..."
cron

# Handle existing logs at startup
LOG_FILE="/var/log/postfiatd/debug.log"
if [ -f "$LOG_FILE" ]; then
    LOG_SIZE=$(stat -c%s "$LOG_FILE" 2>/dev/null || echo 0)
    # If log is over 500MB, truncate directly (rotation would need too much space because of the copy)
    if [ "$LOG_SIZE" -gt 524288000 ]; then
        echo "Log file is $(($LOG_SIZE / 1048576))MB - truncating to free space..."
        truncate -s 0 "$LOG_FILE"
    else
        # Normal rotation for smaller logs
        logrotate -f -s /var/log/postfiatd/.logrotate.status /etc/logrotate.d/postfiatd 2>/dev/null || true
    fi
fi

# Execute postfiatd with any passed arguments
echo "Starting postfiatd..."
exec /usr/local/bin/postfiatd "$@"
