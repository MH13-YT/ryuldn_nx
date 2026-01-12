# ==========================================
# Profile: CRASH (Alias de MINIMAL)
# Détection de crash uniquement
# ==========================================

source gdb/components/base.gdb
source gdb/components/crash.gdb

echo [PROFILE] CRASH: base + crash detection only\n
continue
