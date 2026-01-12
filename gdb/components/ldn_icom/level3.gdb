# ==========================================
# LDN ICommunication - Level 3 (Mode Control)
# AccessPoint/Station mode switching
# ==========================================

break ams::mitm::ldn::ICommunicationService::OpenAccessPoint
    commands
    silent
    printf "[ICOM-L3] OpenAccessPoint()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::CloseAccessPoint
    commands
    silent
    printf "[ICOM-L3] CloseAccessPoint()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::OpenStation
    commands
    silent
    printf "[ICOM-L3] OpenStation()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::CloseStation
    commands
    silent
    printf "[ICOM-L3] CloseStation()\n"
    continue
end

echo [ICOM] Level 3 loaded (4 functions)\n
