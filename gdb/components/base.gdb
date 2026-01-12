# ==========================================
# GDB Base Configuration
# Essential settings for ryuldn_nx debugging
# ==========================================

# Avoid pagination
set pagination off

# Print array elements on separate lines
set print pretty on

# Print full contents of large arrays
set print elements 0

# Catch signals
handle SIGSEGV stop print
handle SIGABRT stop print
handle SIGILL stop print
handle SIGFPE stop print

# Set verbosity
set verbose on

# Command echo
set trace-commands on

# Print timing information
set verbose on

echo [BASE] GDB configuration loaded\n
