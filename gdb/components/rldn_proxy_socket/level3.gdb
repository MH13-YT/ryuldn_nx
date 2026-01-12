# ==========================================
# RyuLDN Proxy Socket - Level 3
# Data transmission
# ==========================================

# Data operations
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::Send
commands
silent
printf "[RLDN-PROXY-SOC] Send(size=%d)\n", $arg2
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::SendTo
commands
silent
printf "[RLDN-PROXY-SOC] SendTo(size=%d, dest=0x%x)\n", $arg2, $arg3
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::Recv
commands
silent
printf "[RLDN-PROXY-SOC] Recv(maxsize=%d)\n", $arg2
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::RecvFrom
commands
silent
printf "[RLDN-PROXY-SOC] RecvFrom(maxsize=%d)\n", $arg2
info locals
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::Accept
commands
silent
printf "[RLDN-PROXY-SOC] Accept() - Accepting connection\n"
bt 1
continue
end

echo [RLDN-PROXY-SOC] Level 3 (5 breakpoints with tracing)\n
