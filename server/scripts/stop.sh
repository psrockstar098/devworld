#!/usr/bin/env bash
# Stop the native platform server. Stores keep running (use infra compose to
# stop those). Never match the process by the invoking shell's command line.
set -euo pipefail
if pkill -x devworld-server 2>/dev/null; then
  echo "stopped devworld-server"
else
  echo "devworld-server is not running"
fi
