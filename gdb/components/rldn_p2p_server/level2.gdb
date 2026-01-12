# ==========================================
# RyuLDN P2P Server - Level 2
# Server initialization and setup
# ==========================================

# Constructor
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::P2pProxyServer
commands
silent
printf "[RLDN-P2P-SRV] Constructor - Initializing P2P server\n"
info locals
continue
end

# Destructor
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::~P2pProxyServer
commands
silent
printf "[RLDN-P2P-SRV] Destructor - Cleanup\n"
continue
end

# Start
break ams::mitm::ldn::ryuldn::proxy::P2pProxyServer::Start
commands
silent
printf "[RLDN-P2P-SRV] Start() - Starting server threads\n"
bt 2
continue
end

echo [RLDN-P2P-SRV] Level 2 (3 breakpoints with tracing)\n
