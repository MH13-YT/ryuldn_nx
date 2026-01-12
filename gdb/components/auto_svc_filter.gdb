# ==========================================
# Auto-Continue on SVC (Simple Mode)
# Automatically continues on SVC syscalls
# Exits GDB immediately on real crashes
# ==========================================

catch signal SIGSEGV
commands
    silent
    # Check if instruction at PC is SVC
    set $instr = *(unsigned int *)$pc
    set $is_svc = (($instr & 0xFF000001) == 0xD4000001)
    
    if $is_svc
        # It's a syscall, continue silently
        continue
    else
        # Real crash - show full dump and EXIT GDB
        printf "\n========================================\n"
        printf "[!!!] REAL SEGFAULT DETECTED\n"
        printf "[!!!] PC: 0x%lx, Instruction: 0x%08x\n", $pc, $instr
        printf "========================================\n\n"
        info registers
        printf "\n===== Code at crash point =====\n"
        x/10i $pc-20
        printf "\n===== Call stack =====\n"
        bt
        printf "\n========================================\n"
        printf "[!!!] CRASH DETECTED - EXITING GDB\n"
        printf "========================================\n\n"
        # Force quit immediately
        quit 1
    end
end

echo [AUTO-CRASH] SIGSEGV handler with auto-continue on SVC enabled\n
