# ==========================================
# Master Proxy Client - Level 2 (State)
# State queries and setters
# ==========================================

break ryuldn::LdnMasterProxyClient::GetState
    commands
    silent
    printf "[MASTER-L2] GetState() -> %u\n", $x0
    continue
end

break ryuldn::LdnMasterProxyClient::GetNetworkInfo
    commands
    silent
    printf "[MASTER-L2] GetNetworkInfo()\n"
    continue
end

break ryuldn::LdnMasterProxyClient::SetNetworkInfo
    commands
    silent
    printf "[MASTER-L2] SetNetworkInfo()\n"
    continue
end

echo [MASTER] Level 2 loaded (3 functions)\n
