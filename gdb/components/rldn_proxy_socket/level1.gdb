# ==========================================
# RyuLDN Proxy Socket - Level 1
# Socket creation and basic queries
# ==========================================

# Query functions
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::GetVirtualIP
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::GetSocket
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::IsConnected

echo [RLDN-PROXY-SOC] Level 1 (3 breakpoints)\n
