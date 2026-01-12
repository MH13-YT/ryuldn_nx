# ==========================================
# RyuLDN P2P Client - Level 2
# Client initialization
# ==========================================

# Constructor/Destructor
break ams::mitm::ldn::ryuldn::proxy::P2pProxyClient::P2pProxyClient
break ams::mitm::ldn::ryuldn::proxy::P2pProxyClient::~P2pProxyClient

# Initialization
break ams::mitm::ldn::ryuldn::proxy::P2pProxyClient::GetProtocol

echo [RLDN-P2P-CLI] Level 2 (3 breakpoints)\n
