# ==========================================
# LDN ICommunication - Level 4 (Setup/Teardown)
# Network creation/destruction, disconnect
# ==========================================

break ams::mitm::ldn::ICommunicationService::CreateNetwork
    commands
    silent
    printf "[ICOM-L4] CreateNetwork()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::DestroyNetwork
    commands
    silent
    printf "[ICOM-L4] DestroyNetwork()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::Disconnect
    commands
    silent
    printf "[ICOM-L4] Disconnect()\n"
    continue
end

echo [ICOM] Level 4 loaded (3 functions)\n
