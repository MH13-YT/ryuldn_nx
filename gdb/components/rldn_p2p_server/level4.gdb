# ==========================================
# RyuLDN P2P Server - Level 4
# Connection management
# ==========================================

# Session management
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::Stop
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::Dispose

# Thread functions
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::LeaseRenewalThreadFunc
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::LeaseRenewalLoop

echo [RLDN-P2P-SRV] Level 4 (4 breakpoints)\n
