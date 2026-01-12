# ==========================================
# RyuLDN P2P Server - Level 1
# Server state queries
# ==========================================

# Query functions
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::IsRunning
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::GetPrivatePort
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::GetPublicPort

echo [RLDN-P2P-SRV] Level 1 (3 breakpoints)\n
