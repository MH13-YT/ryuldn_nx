# ==========================================
# Master Proxy Client - Level 3 (Mode Control)
# AccessPoint/Station mode
# ==========================================

break ryuldn::LdnMasterProxyClient::OpenAccessPoint
    commands
    silent
    printf "[MASTER-L3] OpenAccessPoint()\n"
    continue
end

break ryuldn::LdnMasterProxyClient::CloseAccessPoint
    commands
    silent
    printf "[MASTER-L3] CloseAccessPoint()\n"
    continue
end

echo [MASTER] Level 3 loaded (2 functions)\n
