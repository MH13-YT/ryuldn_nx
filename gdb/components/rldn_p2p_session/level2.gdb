# ==========================================
# RyuLDN P2P Session - Level 2
# Session initialization
# ==========================================

# Constructor/Destructor
break ams::mitm::ldn::ryuldn::proxy::P2pProxySession::P2pProxySession
break ams::mitm::ldn::ryuldn::proxy::P2pProxySession::~P2pProxySession

# Setup
break ams::mitm::ldn::ryuldn::proxy::P2pProxySession::SetIpv4

echo [RLDN-P2P-SES] Level 2 (3 breakpoints)\n
