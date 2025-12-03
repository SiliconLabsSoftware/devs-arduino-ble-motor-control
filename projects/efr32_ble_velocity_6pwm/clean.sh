#!/bin/sh

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Cleaning all build directories in: $SCRIPT_DIR"
echo ""

# Remove all directories containing "build" in their name
find "$SCRIPT_DIR" -maxdepth 1 -type d -name "*build*" -exec rm -rf {} + 2>/dev/null

echo "Cleanup complete"
