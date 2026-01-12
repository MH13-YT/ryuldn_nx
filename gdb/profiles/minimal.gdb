# ==========================================
# Profile: MINIMAL (Level 0 - Crash Only)
# Détection de crash uniquement
# ==========================================

source gdb/components/base.gdb
source gdb/components/crash.gdb

echo [PROFILE] MINIMAL: base + crash only\n
continue
