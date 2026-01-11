#!/bin/bash

# Automated debugging script - runs continuously with full logging
# Version 2.0 - Updated for 100% Ryujinx conformance
# Logs everything to debug_session.log

echo "========================================"
echo "Automated ryuldn_nx Debug Monitor v2.0"
echo "========================================"
echo ""
echo "This script will:"
echo "  1. Compile the latest ryuldn_nx code"
echo "  2. Connect to your Switch automatically"
echo "  3. Detect the correct base address"
echo "  4. Set all breakpoints with auto-continue"
echo "  5. Log all function calls to debug_session.log"
echo "  6. Capture full crash information if a crash occurs"
echo ""
echo "New features being monitored:"
echo "  - Network timeout management (NetworkTimeout class)"
echo "  - P2P proxy integration with UPnP NAT punch"
echo "  - Immediate NetworkChange event delivery"
echo "  - Scan operation with timeout/retry logic"
echo ""
echo "The program will run continuously without stopping"
echo "All information is logged to: debug_session.log"
echo ""
echo "Press Ctrl+C to stop monitoring"
echo ""

cd "$(dirname "$0")"

# First, compile the latest code
echo "Compiling ryuldn_nx with latest changes..."
cd ryuldn_nx
make clean
if [ $? -ne 0 ]; then
    echo "ERROR: Make clean failed"
    exit 1
fi

make
if [ $? -ne 0 ]; then
    echo "ERROR: Compilation failed"
    exit 1
fi
cd ..

echo "Compilation successful!"
echo ""

# Get the current base address dynamically
echo "Detecting base address from Switch..."
BASE_ADDR=$(/c/devkitpro/devkitA64/bin/aarch64-none-elf-gdb.exe \
    -batch \
    -ex "set pagination off" \
    -ex "set height 0" \
    -ex "set architecture aarch64" \
    -ex "target extended-remote 192.168.1.25:22225" \
    -ex "attach 134" \
    -ex "monitor get mappings 134" \
    -ex "quit" 2>&1 | grep "r-x Code" | head -1 | awk '{print $1}')

if [ -z "$BASE_ADDR" ]; then
    echo "ERROR: Could not detect base address"
    echo "Using default: 0x75b6800000"
    BASE_ADDR="0x75b6800000"
else
    echo "Base address detected: $BASE_ADDR"
fi

# Update the auto_debug.gdb file with the correct address
sed -i "s/add-symbol-file ryuldn_nx\/ryuldn_nx.elf 0x[0-9a-f]*/add-symbol-file ryuldn_nx\/ryuldn_nx.elf $BASE_ADDR/" auto_debug.gdb

echo "Starting automated debugging session..."
echo "Monitoring new v2.0 features with real-time logging"
echo ""

# Run GDB with the automated script
# -ex commands run BEFORE the script to disable pagination immediately
/c/devkitpro/devkitA64/bin/aarch64-none-elf-gdb.exe -ex "set pagination off" -ex "set height 0" -x auto_debug.gdb

echo ""
echo "========================================"
echo "Debugging session ended"
echo "Check debug_session.log for all captured information"
echo "========================================"
