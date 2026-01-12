# ==========================================
# RyuLDN Proxy Socket - Level 5
# Complex operations and internals
# ==========================================

# Internal protocol handlers
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::HandleProxyConnect
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::HandleProxyConnectReply
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::HandleProxyData
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::HandleProxyDisconnect

# Connection management
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::Connect

echo [RLDN-PROXY-SOC] Level 5 (5 breakpoints)\n
