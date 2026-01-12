# ==========================================
# LDN ICommunication - Level 2 (Lifecycle)
# Initialize/Finalize, event management
# ==========================================

break ams::mitm::ldn::ICommunicationService::Initialize
    commands
    silent
    printf "[ICOM-L2] Initialize(PID=0x%lx)\n", $x1
    continue
end

break ams::mitm::ldn::ICommunicationService::Finalize
    commands
    silent
    printf "[ICOM-L2] Finalize()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::AttachStateChangeEvent
    commands
    silent
    printf "[ICOM-L2] AttachStateChangeEvent()\n"
    continue
end

echo [ICOM] Level 2 loaded (3 functions)\n
