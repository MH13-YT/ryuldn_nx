@echo off & setlocal ENABLEDELAYEDEXPANSION
cd /d "%~dp0"

cls
echo =========================================
echo ryuldn_nx GDB Launcher v3.0
echo Modular Component Architecture
echo =========================================
echo.

REM Config
set "GDB_EXE=C:\devkitpro\devkitA64\bin\aarch64-none-elf-gdb.exe"
set "SWITCH_IP=192.168.1.25"
set "SWITCH_PORT=22225"
set "PROCESS_ID=134"
set "ELF_FILE=ryuldn_nx/ryuldn_nx.elf"
set "LOG_DIR=debug_logs"
set "GDB_DIR=gdb"

REM Profile selection from command line
set "PROFILE="
if not "!%1!"=="" set "PROFILE=!%1!"

REM Verify prerequisites
if not exist "!GDB_EXE!" (
    echo ERROR: GDB not found at !GDB_EXE!
    echo Please install devkitA64 or adjust GDB_EXE path
    pause
    exit /b 1
)

if not exist "!ELF_FILE!" (
    echo ERROR: ELF not found at !ELF_FILE!
    echo Current directory: %CD%
    pause
    exit /b 1
)

if not exist "!GDB_DIR!\components\" (
    echo ERROR: GDB components directory not found
    pause
    exit /b 1
)

REM Create log directory
if not exist "!LOG_DIR!" (
    mkdir "!LOG_DIR!"
    if not exist "!LOG_DIR!" (
        echo ERROR: Could not create !LOG_DIR! directory
        pause
        exit /b 1
    )
    echo [OK] Created !LOG_DIR! directory
)

REM Generate timestamp: DD-MM-YYYY_HH-MM using PowerShell for reliable parsing
for /f "tokens=*" %%a in ('powershell -NoProfile -Command "Get-Date -Format 'dd-MM-yyyy_HH-mm'"') do set "TIMESTAMP=%%a"

REM Create session directory
set "SESSION_DIR=!LOG_DIR!\!TIMESTAMP!"
if not exist "!SESSION_DIR!" mkdir "!SESSION_DIR!"

echo.
echo [*] Session directory: !SESSION_DIR!
echo [*] Detecting base address...

REM Detect base address from PC (ASLR for sysmodules)
echo [*] Step 1: Connecting to Switch to detect base address...
echo [*] Waiting for connection (this may take a while)...

set "DETECT_FILE=!SESSION_DIR!\mapping.txt"
set "RETRY_COUNT=0"
set "MAX_RETRIES=10"

:retry_connect
set /a RETRY_COUNT+=1
if !RETRY_COUNT! GTR 1 (
    echo [*] Retry attempt !RETRY_COUNT!/!MAX_RETRIES!...
)

REM Set longer timeout for connection (60 seconds per attempt)
"!GDB_EXE!" -batch -q ^
    -ex "set tcp connect-timeout 60" ^
    -ex "target extended-remote !SWITCH_IP!:!SWITCH_PORT!" ^
    -ex "attach !PROCESS_ID!" ^
    -ex "info registers pc" ^
    -ex "detach" ^
    -ex "quit" > "!DETECT_FILE!" 2>&1

REM Extract PC value
set "PC_RAW="
for /f "tokens=2" %%a in ('findstr /C:"pc " "!DETECT_FILE!"') do set "PC_RAW=%%a"

if not defined PC_RAW (
    if !RETRY_COUNT! LSS !MAX_RETRIES! (
        echo [!] Connection failed, retrying in 3 seconds...
        timeout /t 3 /nobreak >nul
        goto :retry_connect
    )
    echo [!] Could not detect PC after !MAX_RETRIES! attempts, using default base address
    set "BASE_ADDR=0x05da400000"
    goto :addr_found
)

echo [*] Detected PC: !PC_RAW!

REM Remove 0x and leading zeros: 0x00000017b8e3c8a4 -> 17b8e3c8a4
set "PC_HEX=!PC_RAW:~2!"
:strip_zeros
if "!PC_HEX:~0,1!"=="0" (
    set "PC_HEX=!PC_HEX:~1!"
    goto :strip_zeros
)

REM Calculate length
set "PC_LEN=0"
set "PC_TMP=!PC_HEX!"
:calc_len
if defined PC_TMP (
    set "PC_TMP=!PC_TMP:~1!"
    set /a PC_LEN+=1
    goto :calc_len
)

REM Align to 2MB boundary (remove last 5 hex digits)
set /a "BASE_LEN=!PC_LEN!-5"
if !BASE_LEN! LSS 1 set "BASE_LEN=1"

REM Extract base prefix
call set "BASE_PREFIX=%%PC_HEX:~0,%BASE_LEN%%%"
set "BASE_ADDR=0x!BASE_PREFIX!00000"
echo [*] Calculated base address: !BASE_ADDR! (2MB aligned)

:addr_found
echo [OK] Base address ready: !BASE_ADDR!
echo.
echo [*] Step 2: Creating GDB session with symbols...

REM Create init script
set "INIT_FILE=!SESSION_DIR!\init.gdb"
set "LOG_FILE_GDB=!SESSION_DIR!/session.log"
(
    echo # ryuldn_nx GDB Session
    echo # Generated: !TIMESTAMP!
    echo # Base address: !BASE_ADDR!
    echo.
    echo set pagination off
    echo set height 0
    echo set width 200
    echo set print pretty on
    echo set logging file !LOG_FILE_GDB!
    echo set logging enabled on
    echo.
    echo target extended-remote !SWITCH_IP!:!SWITCH_PORT!
    echo attach !PROCESS_ID!
    echo.
    echo # Current state
    echo echo \n===== Current PC and threads =====\n
    echo info registers pc
    echo info threads
    echo.
    echo # Load symbols at calculated base
    echo add-symbol-file !ELF_FILE! !BASE_ADDR!
    echo.
    echo # Verify symbols
    echo echo \n===== Symbol verification =====\n
    echo info functions Initialize
    echo x/5i $pc
    echo.
    echo handle SIGINT nostop noprint
    echo handle SIGTERM nostop noprint
    echo.
    echo echo \n
    echo echo [GDB] Connected to !SWITCH_IP!:!SWITCH_PORT! - PID !PROCESS_ID!\n
    echo echo [GDB] Base: !BASE_ADDR! - Log: !LOG_FILE_GDB!\n
    echo echo \n
) > "!INIT_FILE!"

echo [OK] Init script created

REM Profile selection menu
if "!PROFILE!"=="" (
    echo.
    echo Available Profiles:
    echo   0. quick          - Fast: Essential breakpoints only (~8 BPs)
    echo   1. minimal/crash  - Crash detection only (Level 0)
    echo   2. light          - Level 1: Init/finalize all components (~30 BPs)
    echo   3. standard       - Level 2: + Constructors/lifecycle (~60 BPs)
    echo   4. detailed       - Level 3: + Network operations (~100 BPs)
    echo   5. full           - Level 4: + Connections/discovery (~140 BPs)
    echo   6. maximum        - Level 5: All + memory analysis (~174 BPs)
    echo.
    echo   Note: More breakpoints = slower performance
    echo.
    set /p PROFILE="Select profile (0-6) [default: 0]: "
    if "!PROFILE!"=="" set "PROFILE=0"
    
    if "!PROFILE!"=="0" set "PROFILE=quick"
    if "!PROFILE!"=="1" set "PROFILE=minimal"
    if "!PROFILE!"=="2" set "PROFILE=light"
    if "!PROFILE!"=="3" set "PROFILE=standard"
    if "!PROFILE!"=="4" set "PROFILE=detailed"
    if "!PROFILE!"=="5" set "PROFILE=full"
    if "!PROFILE!"=="6" set "PROFILE=maximum"
)

REM Validate profile
if not exist "!GDB_DIR!\profiles\!PROFILE!.gdb" (
    echo ERROR: Unknown profile: !PROFILE!
    echo Available: quick, minimal, crash, light, standard, detailed, full, maximum
    pause
    exit /b 1
)

REM Create combined GDB script
set "RUN_FILE=!SESSION_DIR!\run.gdb"
set "INIT_FILE_GDB=!INIT_FILE:\=/!"
set "PROFILE_FILE_GDB=!GDB_DIR!\profiles\!PROFILE!.gdb"
set "PROFILE_FILE_GDB=!PROFILE_FILE_GDB:\=/!"
(
    echo # Master startup script
    echo source !INIT_FILE_GDB!
    echo source !PROFILE_FILE_GDB!
    echo.
    echo echo \n
    echo echo [PROFILE] Loaded: !PROFILE!\n
    echo echo \n
) > "!RUN_FILE!"

echo [OK] Profile: !PROFILE!
echo.
echo =========================================
echo Starting GDB Session
echo =========================================
echo Profile:   !PROFILE!
echo Session:   !SESSION_DIR!
echo Log:       session.log
echo.
echo Press Ctrl+C in GDB to interrupt
echo.

REM Launch GDB
"!GDB_EXE!" -x "!RUN_FILE!"

REM Session summary
cls
echo =========================================
echo Debug Session Complete
echo =========================================
echo.
echo Session Directory: !SESSION_DIR!
echo.
echo Files:
echo   - session.log    (GDB output)
echo   - init.gdb       (initialization script)
echo   - run.gdb        (run script with profile)
echo   - mapping.txt    (base address detection)
echo.
echo Profile:  !PROFILE!
echo Timestamp: !TIMESTAMP!
echo.
echo To analyze crashes:
echo   python parse_debug.py !SESSION_DIR!\session.log
echo.
echo To search logs:
echo   findstr "CRASH" !SESSION_DIR!\session.log
echo   findstr "ERROR" !SESSION_DIR!\session.log
echo   findstr "Breakpoint" !SESSION_DIR!\session.log
echo.
pause

