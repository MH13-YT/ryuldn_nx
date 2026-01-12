# ==========================================
# Signal & Crash Handlers
# Auto-filters SVC syscalls from real crashes
# ==========================================

# Catch exceptions
catch throw
catch catch

# SIGSEGV with auto-continue on SVC - filters syscalls automatically
source gdb/components/auto_svc_filter.gdb

# Other signals (always real crashes)
catch signal SIGABRT
catch signal SIGILL
catch signal SIGFPE

echo [CRASH] Signal handlers registered with SVC auto-filter\n
