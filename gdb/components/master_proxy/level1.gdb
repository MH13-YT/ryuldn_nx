# ==========================================
# Master Proxy Client - Level 1 (Initialization)
# Simple init/finalize
# ==========================================

break ryuldn::LdnMasterProxyClient::Initialize
    commands
    silent
    printf "[MASTER-L1] Initialize()\n"
    continue
end

break ryuldn::LdnMasterProxyClient::Finalize
    commands
    silent
    printf "[MASTER-L1] Finalize()\n"
    continue
end

echo [MASTER] Level 1 loaded (2 functions)\n
