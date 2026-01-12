# ==========================================
# Profile: QUICK (Fast Performance)
# Only essential breakpoints for optimization verification
# ==========================================

source gdb/components/base.gdb
source gdb/components/crash.gdb

# BufferPool - Only initialization
break ams::mitm::ldn::ryuldn::InitializeBufferPool
    commands
    silent
    printf "[BUFPOOL] Initialization called\n"
    continue
end

# P2P Server - Only lifecycle
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::Start
    commands
    silent
    printf "[P2P-SRV] Server starting\n"
    continue
end

break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::Stop
    commands
    silent
    printf "[P2P-SRV] Server stopping\n"
    continue
end

# Session Pool - Only acquire/release
break ams::mitm::ldn::ryuldn::proxy::SessionPool::Acquire
    commands
    silent
    printf "[SESSION-POOL] Session acquired\n"
    continue
end

break ams::mitm::ldn::ryuldn::proxy::SessionPool::Release
    commands
    silent
    printf "[SESSION-POOL] Session released\n"
    continue
end

# Master proxy - Only connect/disconnect
break ams::mitm::ldn::ryuldn::LdnMasterProxyClient::EnsureConnected
    commands
    silent
    printf "[MASTER] Connecting to master server\n"
    continue
end

break ams::mitm::ldn::ryuldn::LdnMasterProxyClient::Disconnect
    commands
    silent
    printf "[MASTER] Disconnecting\n"
    continue
end

echo [PROFILE] QUICK: Fast performance mode with essential breakpoints only (8 total)\n
continue
