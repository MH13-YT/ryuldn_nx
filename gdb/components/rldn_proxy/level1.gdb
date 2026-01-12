# ==========================================
# RyuLDN Proxy - Level 1
# Simple proxy queries, no side effects
# ==========================================

# Query functions
break ams::mitm::ldn::ryuldn::proxy::LdnProxy::GetLocalIP
commands
silent
printf "[RLDN-PROXY] GetLocalIP() called\n"
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxy::IsVirtualIP
commands
silent
printf "[RLDN-PROXY] IsVirtualIP(ip=0x%x)\n", $arg1
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxy::IsBroadcast
commands
silent
printf "[RLDN-PROXY] IsBroadcast(ip=0x%x)\n", $arg1
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxy::Supported
commands
silent
printf "[RLDN-PROXY] Supported(domain=%d, type=%d, proto=%d)\n", $arg1, $arg2, $arg3
continue
end

echo [RLDN-PROXY] Level 1 (4 breakpoints with tracing)\n
