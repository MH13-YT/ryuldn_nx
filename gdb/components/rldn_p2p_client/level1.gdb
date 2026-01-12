# ==========================================
# RyuLDN P2P Client - Level 1
# Client state queries
# ==========================================

# Query functions
break ams::mitm::ldn::ryuldn::proxy::P2pProxyClient::IsConnected
commands
silent
printf "[RLDN-P2P-CLI] IsConnected() = %d\n", $rax
continue
end

break ams::mitm::ldn::ryuldn::proxy::P2pProxyClient::IsReady
commands
silent
printf "[RLDN-P2P-CLI] IsReady() = %d\n", $rax
continue
end

break ams::mitm::ldn::ryuldn::proxy::P2pProxyClient::GetProxyConfig
commands
silent
printf "[RLDN-P2P-CLI] GetProxyConfig() called\n"
info locals
continue
end

echo [RLDN-P2P-CLI] Level 1 (3 breakpoints with tracing)\n
