# ==========================================
# Profile: FULL (Level 4)
# Connexions, découverte réseau, gestion d'erreurs
# ==========================================

source gdb/components/base.gdb
source gdb/components/crash.gdb
source gdb/components/ldn_icom/level1.gdb
source gdb/components/ldn_icom/level2.gdb
source gdb/components/ldn_icom/level3.gdb
source gdb/components/ldn_icom/level4.gdb
source gdb/components/bsd_mitm/level1.gdb
source gdb/components/bsd_mitm/level2.gdb
source gdb/components/bsd_mitm/level3.gdb
source gdb/components/bsd_mitm/level4.gdb
source gdb/components/master_proxy/level1.gdb
source gdb/components/master_proxy/level2.gdb
source gdb/components/master_proxy/level3.gdb
source gdb/components/master_proxy/level4.gdb
source gdb/components/ldn_monitor/level1.gdb
source gdb/components/ldn_monitor/level2.gdb
source gdb/components/ldn_monitor/level3.gdb
source gdb/components/ldn_monitor/level4.gdb
source gdb/components/rldn_protocol/level1.gdb
source gdb/components/rldn_protocol/level2.gdb
source gdb/components/rldn_protocol/level3.gdb
source gdb/components/rldn_protocol/level4.gdb
source gdb/components/rldn_proxy/level1.gdb
source gdb/components/rldn_proxy/level2.gdb
source gdb/components/rldn_proxy_socket/level1.gdb
source gdb/components/rldn_proxy_socket/level2.gdb
source gdb/components/rldn_proxy_socket/level3.gdb
source gdb/components/rldn_proxy_socket/level4.gdb
source gdb/components/rldn_p2p_client/level1.gdb
source gdb/components/rldn_p2p_client/level2.gdb
source gdb/components/rldn_p2p_client/level3.gdb
source gdb/components/rldn_p2p_client/level4.gdb
source gdb/components/rldn_p2p_server/level1.gdb
source gdb/components/rldn_p2p_server/level2.gdb
source gdb/components/rldn_p2p_server/level3.gdb
source gdb/components/rldn_p2p_server/level4.gdb
source gdb/components/rldn_p2p_session/level1.gdb
source gdb/components/rldn_p2p_session/level2.gdb
source gdb/components/rldn_p2p_session/level3.gdb
source gdb/components/rldn_p2p_session/level4.gdb
source gdb/components/rldn_upnp/level1.gdb
source gdb/components/rldn_upnp/level2.gdb
source gdb/components/rldn_upnp/level3.gdb
source gdb/components/rldn_upnp/level4.gdb

echo [PROFILE] FULL (Level 4): base + crash + L1-L4 all components\n
continue
