# ==========================================
# RyuLDN P2P Session - Level 1
# Session state queries
# ==========================================

# Query functions
break ams::mitm::ldn::ryuldn::proxy::P2pProxySession::GetVirtualIpAddress
break ams::mitm::ldn::ryuldn::proxy::P2pProxySession::GetSocket
break ams::mitm::ldn::ryuldn::proxy::P2pProxySession::GetProtocol

echo [RLDN-P2P-SES] Level 1 (3 breakpoints)\n
