# ==========================================
# RyuLDN Proxy - Level 4
# Connection management
# ==========================================

# Connection ops
break ams::mitm::ldn::ryuldn::proxy::LdnProxy::RequestConnection
break ams::mitm::ldn::ryuldn::proxy::LdnProxy::SignalConnected
break ams::mitm::ldn::ryuldn::proxy::LdnProxy::EndConnection

# Cleanup
break ams::mitm::ldn::ryuldn::proxy::LdnProxy::CleanupSocket

echo [RLDN-PROXY] Level 4 (4 breakpoints)\n
