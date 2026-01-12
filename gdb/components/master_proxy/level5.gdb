# ==========================================
# Master Proxy Client - Level 5 (Scan)
# Complex scan operations
# ==========================================

break ryuldn::LdnMasterProxyClient::Scan
    commands
    silent
    printf "[MASTER-L5] Scan(channel=%u)\n", $x1
    continue
end

echo [MASTER] Level 5 loaded (1 function)\n
