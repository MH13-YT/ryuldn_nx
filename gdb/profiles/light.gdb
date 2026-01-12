# ==========================================
# Profile: LIGHT (Level 1)
# Tous les composants niveau 1 (init/finalize, opérations simples)
# ==========================================

source gdb/components/base.gdb
source gdb/components/crash.gdb
source gdb/components/ldn_icom/level1.gdb
source gdb/components/bsd_mitm/level1.gdb
source gdb/components/master_proxy/level1.gdb
source gdb/components/ldn_monitor/level1.gdb
source gdb/components/rldn_protocol/level1.gdb
source gdb/components/rldn_proxy/level1.gdb
source gdb/components/rldn_proxy_socket/level1.gdb
source gdb/components/rldn_p2p_client/level1.gdb
source gdb/components/rldn_p2p_server/level1.gdb
source gdb/components/rldn_p2p_session/level1.gdb
source gdb/components/rldn_upnp/level1.gdb

echo [PROFILE] LIGHT (Level 1): base + crash + L1 all components\n
continue
