# ==========================================
# RyuLDN P2P Client - Level 5
# Complex operations and internals
# ==========================================

# Thread functions
break ams::mitm::ldn::ryuldn::proxy::P2pProxyClient::ReceiveThreadFunc
break ams::mitm::ldn::ryuldn::proxy::P2pProxyClient::ReceiveLoop

# Protocol handlers
break ams::mitm::ldn::ryuldn::proxy::P2pProxyClient::HandleProxyConfig

echo [RLDN-P2P-CLI] Level 5 (3 breakpoints)\n
