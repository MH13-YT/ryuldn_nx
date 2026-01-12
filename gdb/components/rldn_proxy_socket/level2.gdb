# ==========================================
# RyuLDN Proxy Socket - Level 2
# Socket binding and connection
# ==========================================

# Constructor
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::LdnProxySocket
commands
silent
printf "[RLDN-PROXY-SOC] Constructor - Creating socket\n"
info locals
continue
end

# Destructor
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::~LdnProxySocket
commands
silent
printf "[RLDN-PROXY-SOC] Destructor - Cleaning up socket\n"
bt 1
continue
end

# Socket binding
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::Bind
commands
silent
printf "[RLDN-PROXY-SOC] Bind() called\n"
info locals
continue
end

# Listen
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::Listen
commands
silent
printf "[RLDN-PROXY-SOC] Listen() - Accepting connections\n"
continue
end

echo [RLDN-PROXY-SOC] Level 2 (4 breakpoints with tracing)\n
