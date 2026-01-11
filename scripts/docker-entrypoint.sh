#!/bin/bash
# Docker entrypoint script for postfiatd
# Starts cron for log rotation and then runs postfiatd

set -e

# Start cron daemon in background for log rotation
echo "Starting cron daemon for log rotation..."
cron

# Run logrotate once at startup to handle any large existing logs
# Use a persistent state file location (inside the log volume)
logrotate -f -s /var/log/postfiatd/.logrotate.status /etc/logrotate.d/postfiatd 2>/dev/null || true

# Execute postfiatd with any passed arguments
echo "Starting postfiatd..."
exec /usr/local/bin/postfiatd "$@"
