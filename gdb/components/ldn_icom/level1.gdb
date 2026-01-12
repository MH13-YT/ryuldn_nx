# ==========================================
# LDN ICommunication - Level 1 (Simple)
# Pure state queries, no side effects
# ==========================================

break ams::mitm::ldn::ICommunicationService::GetState
    commands
    silent
    printf "[ICOM-L1] GetState() -> %u\n", $x0
    continue
end

break ams::mitm::ldn::ICommunicationService::GetNetworkInfo
    commands
    silent
    printf "[ICOM-L1] GetNetworkInfo()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::GetIpv4Address
    commands
    silent
    printf "[ICOM-L1] GetIpv4Address()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::GetDisconnectReason
    commands
    silent
    printf "[ICOM-L1] GetDisconnectReason() -> %u\n", $x0
    continue
end

break ams::mitm::ldn::ICommunicationService::GetSecurityParameter
    commands
    silent
    printf "[ICOM-L1] GetSecurityParameter()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::GetNetworkConfig
    commands
    silent
    printf "[ICOM-L1] GetNetworkConfig()\n"
    continue
end

echo [ICOM] Level 1 loaded (6 functions)\n
