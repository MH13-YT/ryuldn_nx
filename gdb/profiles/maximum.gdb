# ==========================================
# Profile: MAXIMUM (Level 5 - Complet)
# Tous les composants, tous les niveaux + analyse mémoire
# Logging le plus exhaustif possible
# ==========================================

source gdb/components/base.gdb
source gdb/components/crash.gdb
source gdb/components/ldn_icom/level1.gdb
source gdb/components/ldn_icom/level2.gdb
source gdb/components/ldn_icom/level3.gdb
source gdb/components/ldn_icom/level4.gdb
source gdb/components/ldn_icom/level5.gdb
source gdb/components/bsd_mitm/level1.gdb
source gdb/components/bsd_mitm/level2.gdb
source gdb/components/bsd_mitm/level3.gdb
source gdb/components/bsd_mitm/level4.gdb
source gdb/components/bsd_mitm/level5.gdb
source gdb/components/master_proxy/level1.gdb
source gdb/components/master_proxy/level2.gdb
source gdb/components/master_proxy/level3.gdb
source gdb/components/master_proxy/level4.gdb
source gdb/components/master_proxy/level5.gdb
source gdb/components/ldn_monitor/level1.gdb
source gdb/components/ldn_monitor/level2.gdb
source gdb/components/ldn_monitor/level3.gdb
source gdb/components/ldn_monitor/level4.gdb
source gdb/components/ldn_monitor/level5.gdb
source gdb/components/rldn_protocol/level1.gdb
source gdb/components/rldn_protocol/level2.gdb
source gdb/components/rldn_protocol/level3.gdb
source gdb/components/rldn_protocol/level4.gdb
source gdb/components/rldn_protocol/level5.gdb
source gdb/components/rldn_proxy/level1.gdb
source gdb/components/rldn_proxy/level2.gdb
source gdb/components/rldn_proxy_socket/level1.gdb
source gdb/components/rldn_proxy_socket/level2.gdb
source gdb/components/rldn_proxy_socket/level3.gdb
source gdb/components/rldn_proxy_socket/level4.gdb
source gdb/components/rldn_proxy_socket/level5.gdb
source gdb/components/rldn_p2p_client/level1.gdb
source gdb/components/rldn_p2p_client/level2.gdb
source gdb/components/rldn_p2p_client/level3.gdb
source gdb/components/rldn_p2p_client/level4.gdb
source gdb/components/rldn_p2p_client/level5.gdb
source gdb/components/rldn_p2p_server/level1.gdb
source gdb/components/rldn_p2p_server/level2.gdb
source gdb/components/rldn_p2p_server/level3.gdb
source gdb/components/rldn_p2p_server/level4.gdb
source gdb/components/rldn_p2p_server/level5.gdb
source gdb/components/rldn_p2p_session/level1.gdb
source gdb/components/rldn_p2p_session/level2.gdb
source gdb/components/rldn_p2p_session/level3.gdb
source gdb/components/rldn_p2p_session/level4.gdb
source gdb/components/rldn_p2p_session/level5.gdb
source gdb/components/rldn_upnp/level1.gdb
source gdb/components/rldn_upnp/level2.gdb
source gdb/components/rldn_upnp/level3.gdb
source gdb/components/rldn_upnp/level4.gdb
source gdb/components/rldn_upnp/level5.gdb
source gdb/components/memory.gdb

echo [PROFILE] MAXIMUM (Level 5): base + crash + L1-L5 all components + memory analysis\n
echo [GDB] Starting execution...\n
continue
