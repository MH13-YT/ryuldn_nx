# ==========================================
# RyuLDN P2P Server - Level 3
# Connection acceptance
# ==========================================

# Accept connections
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::AcceptLoop
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::AcceptThreadFunc

# Port mapping
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::RefreshLease

echo [RLDN-P2P-SRV] Level 3 (3 breakpoints)\n
