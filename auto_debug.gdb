# Automated GDB debugging script with full logging
# Version 2.0 - Updated for 100% Ryujinx conformance
# This script monitors all critical LDN operations including:
#   - Network timeout management (NetworkTimeout class)
#   - P2P proxy configuration and integration
#   - Immediate NetworkChange event delivery
#   - Scan operation with timeout/retry logic

# Set architecture
set architecture aarch64

# Disable pagination FIRST - before any output
set pagination off
set height 0
set width 0

# Enable logging to file
set logging file debug_session.log
set logging overwrite off
set logging redirect off
set logging enabled on

echo \n
echo ==========================================\n
echo Automated Debugging Session Started v2.0\n
echo ==========================================\n
echo Features being monitored:\n
echo   - NetworkTimeout class creation/disposal\n
echo   - P2P proxy server integration\n
echo   - Immediate NetworkChange events\n
echo   - Scan timeout and retry logic\n
echo   - Network connection state transitions\n
echo Logging to: debug_session.log\n
echo \n

# Connect to Switch
target extended-remote 192.168.1.25:22225

# Attach to process
attach 134

# Detect and load symbols automatically
echo Detecting base address...\n
monitor get mappings 134

# Extract address from mappings output and load symbols
shell echo "Attempting to load symbols..."

# Load symbols - this address will be updated by the batch script
add-symbol-file ryuldn_nx/ryuldn_nx.elf 0x214ba00000

# Enable pretty printing
set print pretty on
set print array on
set print array-indexes on

# Configure to not stop on common signals
handle SIGINT nostop pass
handle SIGTERM nostop pass

# =====================================
# NEW: Network Timeout Monitoring
# =====================================
echo \n[SETUP] Adding NetworkTimeout breakpoints...\n

# NetworkTimeout constructor
break ams::mitm::ldn::ryuldn::NetworkTimeout::NetworkTimeout
commands
    silent
    echo \n--- NetworkTimeout constructor ---\n
    backtrace
    info args
    continue
end

# NetworkTimeout RefreshTimeout - indicates scan timeout reset
break ams::mitm::ldn::ryuldn::NetworkTimeout::RefreshTimeout
commands
    silent
    echo [TIMEOUT] RefreshTimeout called\n
    backtrace
    continue
end

# NetworkTimeout DisableTimeout - cleanup on success
break ams::mitm::ldn::ryuldn::NetworkTimeout::DisableTimeout
commands
    silent
    echo [TIMEOUT] DisableTimeout called - operation complete\n
    backtrace
    continue
end

# =====================================
# NEW: P2P Proxy Integration Monitoring
# =====================================
echo [SETUP] Adding P2P Proxy breakpoints...\n

# ConfigureAccessPoint - P2P setup
break ams::mitm::ldn::ryuldn::LdnMasterProxyClient::ConfigureAccessPoint
commands
    silent
    echo \n--- ConfigureAccessPoint (P2P Proxy) ---\n
    backtrace
    info args
    info locals
    echo Checking proxy configuration...\n
    continue
end

# DisconnectProxy - cleanup
break ams::mitm::ldn::ryuldn::LdnMasterProxyClient::DisconnectProxy
commands
    silent
    echo [PROXY] DisconnectProxy called - cleaning up\n
    backtrace
    continue
end

# =====================================
# NEW: Immediate NetworkChange Event
# =====================================
echo [SETUP] Adding NetworkChange event breakpoints...\n

# CreateNetwork - where immediate event is sent
break ams::mitm::ldn::ryuldn::LdnMasterProxyClient::CreateNetwork
commands
    silent
    echo \n========================================\n
    echo [NETWORK EVENT] CreateNetwork called\n
    echo ========================================\n
    echo Immediate NetworkChange event should fire here\n
    info threads
    backtrace
    info args
    echo ========================================\n
    continue
end

# =====================================
# Core ICommunicationService breakpoints
# =====================================
echo [SETUP] Adding ICommunicationService breakpoints...\n

break ams::mitm::ldn::ICommunicationService::Initialize
commands
    silent
    echo \n========================================\n
    echo [BREAKPOINT] Initialize called\n
    echo ========================================\n
    info threads
    backtrace
    info locals
    echo ========================================\n
    continue
end

break ams::mitm::ldn::ICommunicationService::Scan
commands
    silent
    echo \n========================================\n
    echo [BREAKPOINT] Scan called\n
    echo Timeout logic: RefreshTimeout, clear results, 1s wait\n
    echo ========================================\n
    info threads
    backtrace
    info locals
    echo ========================================\n
    continue
end

break ams::mitm::ldn::ICommunicationService::Connect
commands
    silent
    echo \n========================================\n
    echo [BREAKPOINT] Connect called\n
    echo ========================================\n
    info threads
    backtrace
    info locals
    echo ========================================\n
    continue
end

break ams::mitm::ldn::ICommunicationService::OpenAccessPoint
commands
    silent
    echo \n========================================\n
    echo [BREAKPOINT] OpenAccessPoint called\n
    echo ========================================\n
    info threads
    backtrace
    info locals
    echo ========================================\n
    continue
end

break ams::mitm::ldn::ICommunicationService::OpenStation
commands
    silent
    echo \n========================================\n
    echo [BREAKPOINT] OpenStation called\n
    echo ========================================\n
    info threads
    backtrace
    info locals
    echo ========================================\n
    continue
end

# LdnMasterProxyClient constructor - watch for initialization
break ams::mitm::ldn::ryuldn::LdnMasterProxyClient::LdnMasterProxyClient
commands
    silent
    echo \n========================================\n
    echo [INIT] LdnMasterProxyClient constructor\n
    echo Creating NetworkTimeout, callbacks, proxies...\n
    echo ========================================\n
    info threads
    backtrace
    info args
    info locals
    echo ========================================\n
    continue
end

# Catch crashes and log everything
catch signal SIGSEGV
commands
    silent
    echo \n\n
    echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n
    echo [CRASH DETECTED] Segmentation Fault\n
    echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n
    echo \n
    echo Time of crash:\n
    shell date
    echo \n
    echo Thread information:\n
    info threads
    echo \n
    echo Full backtrace:\n
    thread apply all backtrace
    echo \n
    echo Current thread backtrace with locals:\n
    backtrace full
    echo \n
    echo Register dump:\n
    info registers
    echo \n
    echo Memory around PC:\n
    x/20i $pc-40
    echo \n
    echo Stack dump:\n
    x/32xg $sp
    echo \n
    echo Memory mappings:\n
    monitor get mappings 134
    echo \n
    echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n
    echo Crash analysis complete - logged to file\n
    echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n
    echo \n
end

echo \n
echo ==========================================\n
echo Setup complete - Starting continuous monitoring\n
echo ==========================================\n
echo All breakpoints set with auto-continue\n
echo Monitoring:\n
echo   - Network timeout operations\n
echo   - P2P proxy integration\n
echo   - NetworkChange events\n
echo   - Scan operations with retry logic\n
echo Crash detection active\n
echo Logging everything to: debug_session.log\n
echo \n
echo Press Ctrl+C to stop monitoring\n
echo ==========================================\n
echo \n

# Start continuous execution
continue
    info threads
    backtrace
    info locals
    echo ========================================\n
    continue
end

break ams::mitm::ldn::ICommunicationService::OpenAccessPoint
commands
    silent
    echo \n========================================\n
    echo [BREAKPOINT] OpenAccessPoint called\n
    echo ========================================\n
    info threads
    backtrace
    info locals
    echo ========================================\n
    continue
end

break ams::mitm::ldn::ICommunicationService::OpenStation
commands
    silent
    echo \n========================================\n
    echo [BREAKPOINT] OpenStation called\n
    echo ========================================\n
    info threads
    backtrace
    info locals
    echo ========================================\n
    continue
end

# LdnMasterProxyClient constructor - this is where the crash happens
break ryuldn::LdnMasterProxyClient::LdnMasterProxyClient
commands
    silent
    echo \n========================================\n
    echo [BREAKPOINT] LdnMasterProxyClient constructor\n
    echo ========================================\n
    echo WARNING: Crash typically occurs here\n
    info threads
    backtrace
    info args
    info locals
    info registers
    echo Checking this pointer...\n
    print this
    echo ========================================\n
    continue
end

# Catch crashes and log everything
catch signal SIGSEGV
commands
    silent
    echo \n\n
    echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n
    echo [CRASH DETECTED] Segmentation Fault\n
    echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n
    echo \n
    echo Time of crash:\n
    shell date
    echo \n
    echo Thread information:\n
    info threads
    echo \n
    echo Full backtrace:\n
    thread apply all backtrace
    echo \n
    echo Current thread backtrace with locals:\n
    backtrace full
    echo \n
    echo Register dump:\n
    info registers
    echo \n
    echo Memory around PC:\n
    x/20i $pc-40
    echo \n
    echo Stack dump:\n
    x/32xg $sp
    echo \n
    echo Memory mappings:\n
    monitor get mappings 134
    echo \n
    echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n
    echo Crash analysis complete - logged to file\n
    echo !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n
    echo \n
    # Don't continue after crash - stay for manual inspection
    # But log is already saved
end

echo \n
echo ========================================\n
echo Setup complete - Starting continuous monitoring\n
echo ========================================\n
echo All breakpoints set with auto-continue\n
echo Crash detection active\n
echo Logging everything to: debug_session.log\n
echo \n
echo Press Ctrl+C to stop monitoring\n
echo ========================================\n
echo \n

# Start continuous execution
continue
