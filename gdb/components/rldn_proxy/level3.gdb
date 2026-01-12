# ==========================================
# RyuLDN Proxy - Level 3
# Mode control and forwarding
# ==========================================

# Port management
break ams::mitm::ldn::ryuldn::proxy::LdnProxy::GetEphemeralPort
commands
silent
printf "[RLDN-PROXY] GetEphemeralPort(proto=%d)\n", $arg1
info locals
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxy::ReturnEphemeralPort
commands
silent
printf "[RLDN-PROXY] ReturnEphemeralPort(proto=%d, port=%d)\n", $arg1, $arg2
continue
end

# Socket registration
break ams::mitm::ldn::ryuldn::proxy::LdnProxy::RegisterSocket
commands
silent
printf "[RLDN-PROXY] RegisterSocket() called\n"
bt 1
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxy::UnregisterSocket
commands
silent
printf "[RLDN-PROXY] UnregisterSocket() called\n"
continue
end

echo [RLDN-PROXY] Level 3 (4 breakpoints with tracing)\n
