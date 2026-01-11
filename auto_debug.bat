@echo off
REM Automated debugging script for Windows - runs continuously with full logging
REM Updated for ryuldn_nx with 100%% Ryujinx conformance implementation

echo ========================================
echo Automated ryuldn_nx Debug Monitor v2.0
echo ========================================
echo.
echo This script will:
echo   1. Compile the latest ryuldn_nx code
echo   2. Connect to your Switch automatically
echo   3. Detect the correct base address
echo   4. Set all breakpoints with auto-continue
echo   5. Log all function calls to debug_session.log
echo   6. Capture full crash information if a crash occurs
echo.
echo New features:
echo   - Network timeout management monitoring
echo   - P2P proxy integration verification
echo   - Immediate NetworkChange event delivery
echo   - Comprehensive scan operation logging
echo.
echo The program will run continuously without stopping
echo All information is logged to: debug_session.log
echo.
echo Press Ctrl+C to stop monitoring
echo.

cd /d "%~dp0"
echo Detecting base address from Switch...

REM Get base address dynamically
for /f "tokens=1" %%a in ('C:\devkitpro\devkitA64\bin\aarch64-none-elf-gdb.exe -batch -ex "set architecture aarch64" -ex "target extended-remote 192.168.1.25:22225" -ex "attach 134" -ex "monitor get mappings 134" -ex "quit" 2^>^&1 ^| findstr /C:"r-x Code"') do (
    set BASE_ADDR=%%a
    goto :found
)

:found
if "%BASE_ADDR%"=="" (
    echo ERROR: Could not detect base address
    echo Using default: 0x75b6800000
    set BASE_ADDR=0x75b6800000
) else (
    echo Base address detected: %BASE_ADDR%
)

REM Update auto_debug.gdb with correct address
echo Updating GDB script with correct base address...
powershell -Command "(Get-Content auto_debug.gdb) -replace 'add-symbol-file ryuldn_nx/ryuldn_nx.elf 0x[0-9a-f]+', 'add-symbol-file ryuldn_nx/ryuldn_nx.elf %BASE_ADDR%' | Set-Content auto_debug.gdb"

echo Starting automated debugging session...
echo This will compile and run with real-time logging
echo All output saved to debug_session.log
echo.

REM Run GDB with automated script
REM -ex commands run BEFORE the script to disable pagination immediately
C:\devkitpro\devkitA64\bin\aarch64-none-elf-gdb.exe -ex "set pagination off" -ex "set height 0" -x auto_debug.gdb

echo.
echo Debugging session completed.
echo Review debug_session.log for detailed information.
pause
