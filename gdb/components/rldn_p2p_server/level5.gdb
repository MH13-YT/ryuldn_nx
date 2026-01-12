# ==========================================
# RyuLDN P2P Server - Level 5
# Complex operations and internals
# ==========================================

# Protocol handlers
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::HandleToken
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::HandleStateChange
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::RouteMessage

# UPnP operations
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::GetExternalIP

echo [RLDN-P2P-SRV] Level 5 (4 breakpoints)\n
