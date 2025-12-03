#!/bin/sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ACTION="${1:-build}"

if [ "$ACTION" = "clean" ] || [ "$ACTION" = "build" ]; then
    echo "Cleaning projects..."
    for project in "$SCRIPT_DIR/projects"/*; do
        [ -f "$project/clean.sh" ] && sh "$project/clean.sh"
    done
    echo ""
fi

if [ "$ACTION" = "build" ]; then
    echo "Building projects..."
    failed=0
    for project in "$SCRIPT_DIR/projects"/*; do
        if [ -f "$project/build.sh" ]; then
            sh "$project/build.sh" "$project" || failed=1
        fi
    done
    exit $failed
fi
