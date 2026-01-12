# ==========================================
# RyuLDN Proxy Socket - Level 4
# Socket cleanup and management
# ==========================================

# Socket control
break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::Close
commands
silent
printf "[RLDN-PROXY-SOC] Close() - Closing socket\n"
info locals
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::Shutdown
commands
silent
printf "[RLDN-PROXY-SOC] Shutdown(how=%d)\n", $arg1
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::SetSocketOption
commands
silent
printf "[RLDN-PROXY-SOC] SetSocketOption(opt=%d, val=%d)\n", $arg1, $arg2
continue
end

break ams::mitm::ldn::ryuldn::proxy::LdnProxySocket::GetSocketOption
commands
silent
printf "[RLDN-PROXY-SOC] GetSocketOption(opt=%d)\n", $arg1
continue
end

echo [RLDN-PROXY-SOC] Level 4 (4 breakpoints with tracing)\n
