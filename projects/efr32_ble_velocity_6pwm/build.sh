#!/bin/sh

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Define the device matrix
DEVICES="SiliconLabs:silabs:nano_matter SiliconLabs:silabs:thingplusmatter"

# Check if arduino-cli is available
if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "Error: arduino-cli is not installed or not in PATH" >&2
    exit 1
fi

# Parse parameters
DEVICE_PARAM=""
SKETCH_PATH=""

# Check if first parameter is a device or a path
if [ -n "$1" ]; then
    if [ -d "$1" ]; then
        # First param is a directory (sketch path)
        SKETCH_PATH="$1"
        DEVICE_PARAM="$2"
    else
        # First param might be a device name
        DEVICE_PARAM="$1"
        SKETCH_PATH="$2"
    fi
fi

# Set default sketch path if not provided
SKETCH_PATH="${SKETCH_PATH:-$SCRIPT_DIR}"

# Validate sketch path exists
if [ ! -d "$SKETCH_PATH" ]; then
    echo "Error: Sketch path '$SKETCH_PATH' does not exist" >&2
    exit 1
fi

# Determine which devices to build
if [ -z "$DEVICE_PARAM" ]; then
    # No device parameter: build all devices
    BUILD_DEVICES="$DEVICES"
else
    # Device parameter provided: check if it matches
    BUILD_DEVICES=""
    
    for device in $DEVICES; do
        device_name="${device##*:}"
        if [ "$device_name" = "$DEVICE_PARAM" ] || [ "$device" = "$DEVICE_PARAM" ]; then
            BUILD_DEVICES="$device"
            break
        fi
    done
    
    if [ -z "$BUILD_DEVICES" ]; then
        echo "Error: Device '$DEVICE_PARAM' not found in device matrix" >&2
        echo "Available devices:" >&2
        for device in $DEVICES; do
            echo "  - ${device##*:}" >&2
        done
        exit 1
    fi
fi

echo "Building sketches from: $SKETCH_PATH"
echo ""

# Track compilation results
success_count=0
failed_count=0
failed_devices=""

# Loop through selected devices
for device in $BUILD_DEVICES; do
    # Extract device name after last ":"
    device_name="${device##*:}"
    
    # Build directory for this specific device
    build_path="$SCRIPT_DIR/build_$device_name"
    
    # Create build directory if it doesn't exist
    mkdir -p "$build_path"
    
    echo "=========================================="
    echo "Compiling for device: $device"
    echo "Build path: $build_path"
    echo "=========================================="
    
    if arduino-cli compile \
        --fqbn "$device" \
        --board-options protocol_stack=ble_silabs \
        --build-path "$build_path" \
        "$SKETCH_PATH"; then
        echo "Successfully compiled for $device"
        success_count=$((success_count + 1))
    else
        echo "Failed to compile for $device" >&2
        failed_count=$((failed_count + 1))
        failed_devices="$failed_devices  - $device
"
    fi
    echo ""
done

total_count=$((success_count + failed_count))
echo "=========================================="
echo "Compilation complete: $success_count/$total_count succeeded"
if [ $failed_count -gt 0 ]; then
    echo "Failed devices:"
    printf "%s" "$failed_devices"
    echo "=========================================="
    exit 1
fi
echo "=========================================="
