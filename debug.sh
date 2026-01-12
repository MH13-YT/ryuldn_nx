#!/bin/bash
# ==========================================
# ryuldn_nx GDB Launcher - Clean System v1.0
# ==========================================
# Detects base address dynamically and launches GDB with appropriate profile

set -e

# Configuration
GDB_EXE="/c/devkitpro/devkitA64/bin/aarch64-none-elf-gdb.exe"
SWITCH_IP="192.168.1.25"
SWITCH_PORT="22225"
PROCESS_ID="134"
ELF_FILE="ryuldn_nx/ryuldn_nx.elf"
LOG_DIR="debug_logs"

# Verify prerequisites
if [ ! -f "$GDB_EXE" ]; then
    echo "ERROR: GDB not found at $GDB_EXE"
    exit 1
fi

if [ ! -f "$ELF_FILE" ]; then
    echo "ERROR: ELF file not found at $ELF_FILE"
    exit 1
fi

mkdir -p "$LOG_DIR"

# Generate timestamp
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

echo "========================================="
echo "ryuldn_nx GDB Launcher v1.0"
echo "========================================="
echo ""

# Step 1: Detect base address
echo "[1/3] Detecting base address from Switch..."

DETECT_FILE="$LOG_DIR/detect_$TIMESTAMP.txt"

{
    echo "target extended-remote $SWITCH_IP:$SWITCH_PORT"
    echo "attach $PROCESS_ID"
    echo "monitor get mappings $PROCESS_ID"
    echo "detach"
    echo "quit"
} | "$GDB_EXE" -batch -q 2>&1 | tee "$DETECT_FILE"

# Parse base address (first r-x section = code)
BASE_ADDR="0x05da400000"
BASE_ADDR=$(grep -oP '^0x[0-9a-f]+(?=.*r-x)' "$DETECT_FILE" | head -1)
BASE_ADDR="${BASE_ADDR:-0x05da400000}"

echo "[OK] Base address: $BASE_ADDR"
echo ""

# Step 2: Create initialization script
echo "[2/2] Creating GDB initialization script..."

INIT_FILE="$LOG_DIR/init_$TIMESTAMP.gdb"

cat > "$INIT_FILE" << EOF
# ==========================================
# ryuldn_nx GDB Initialization
# ==========================================
# Timestamp: $TIMESTAMP
# Base Address: $BASE_ADDR

set architecture aarch64
set pagination off
set height 0
set width 0

set print pretty on
set print array on
set print array-indexes on

# Logging
set logging file $LOG_DIR/session_$TIMESTAMP.log
set logging overwrite off
set logging enabled on

echo \n
echo ==========================================\n
echo GDB Debug Session\n
echo Base Address: $BASE_ADDR\n
echo Timestamp: $TIMESTAMP\n
echo ==========================================\n

# Connect and attach
target extended-remote $SWITCH_IP:$SWITCH_PORT
attach $PROCESS_ID

# Load symbols
add-symbol-file $ELF_FILE $BASE_ADDR

# Signal handling
handle SIGINT nostop pass
handle SIGTERM nostop pass
handle SIGPIPE nostop pass

echo \n[OK] Initialization complete\n
EOF

echo "[OK] Init file: $INIT_FILE"
echo ""

# Step 3: Select profile
echo "========================================="
echo "Select Debug Profile"
echo "========================================="
echo "0. Quick          - Fast: Essential breakpoints only (~8 BPs)"
echo "1. Minimal/Crash  - Crash detection only (Level 0)"
echo "2. Light          - Level 1: Init/finalize all components (~30 BPs)"
echo "3. Standard       - Level 2: + Constructors/lifecycle (~60 BPs)"
echo "4. Detailed       - Level 3: + Network operations (~100 BPs)"
echo "5. Full           - Level 4: + Connections/discovery (~140 BPs)"
echo "6. Maximum        - Level 5: All + memory analysis (~174 BPs)"
echo ""
echo "Note: More breakpoints = slower performance"
echo ""

read -p "Select profile (0-6) [default: 0]: " PROFILE
PROFILE=${PROFILE:-0}

case $PROFILE in
    0)
        PROFILE_NAME="quick"
        PROFILE_FILE="gdb/profiles/quick.gdb"
        ;;
    1)
        PROFILE_NAME="minimal"
        PROFILE_FILE="gdb/profiles/minimal.gdb"
        ;;
    2)
        PROFILE_NAME="light"
        PROFILE_FILE="gdb/profiles/light.gdb"
        ;;
    3)
        PROFILE_NAME="standard"
        PROFILE_FILE="gdb/profiles/standard.gdb"
        ;;
    4)
        PROFILE_NAME="detailed"
        PROFILE_FILE="gdb/profiles/detailed.gdb"
        ;;
    5)
        PROFILE_NAME="full"
        PROFILE_FILE="gdb/profiles/full.gdb"
        ;;
    6)
        PROFILE_NAME="maximum"
        PROFILE_FILE="gdb/profiles/maximum.gdb"
        ;;
    *)
        echo "Invalid choice"
        exit 1
        ;;
esac

# Check if profile exists
if [ ! -f "$PROFILE_FILE" ]; then
    echo "ERROR: Profile not found: $PROFILE_FILE"
    exit 1
fi

# Create run script that combines init + profile
RUN_FILE="$LOG_DIR/run_$TIMESTAMP.gdb"
cat > "$RUN_FILE" << EOF
# Master startup script
source $INIT_FILE
source $PROFILE_FILE

echo \n
echo [PROFILE] Loaded: $PROFILE_NAME\n
echo.
EOF

echo ""
echo "========================================="
echo "Starting GDB - $PROFILE_NAME Profile"
echo "========================================="
echo "Profile: $PROFILE_FILE"
echo "Log:     $LOG_DIR/session_$TIMESTAMP.log"
echo ""

# Launch GDB
$GDB_EXE -x "$RUN_FILE" -q

# Summary
echo ""
echo "========================================="
echo "Debug Session Complete"
echo "========================================="
echo ""
echo "Session Files:"
echo "  Log:      $LOG_DIR/session_$TIMESTAMP.log"
echo "  Init:     $INIT_FILE"
echo "  Run:      $RUN_FILE"
echo "  Mapping:  $DETECT_FILE"
echo ""
echo "Profile:   $PROFILE_NAME"
echo "Timestamp: $TIMESTAMP"
echo ""
echo "To analyze log:"
echo "  python parse_debug.py $LOG_DIR/session_$TIMESTAMP.log"
echo ""
