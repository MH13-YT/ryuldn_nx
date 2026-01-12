# ==========================================
# LDN ICommunication - Level 5 (Complex)
# Connect, Scan, data management, internals
# ==========================================

break ams::mitm::ldn::ICommunicationService::Connect
    commands
    silent
    printf "[ICOM-L5] Connect()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::Scan
    commands
    silent
    printf "[ICOM-L5] Scan(channel=%u)\n", $x3
    continue
end

break ams::mitm::ldn::ICommunicationService::SetAdvertiseData
    commands
    silent
    printf "[ICOM-L5] SetAdvertiseData(size=%u)\n", $x2
    continue
end

break ams::mitm::ldn::ICommunicationService::GetNetworkInfoLatestUpdate
    commands
    silent
    printf "[ICOM-L5] GetNetworkInfoLatestUpdate()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::setState
    commands
    silent
    printf "[ICOM-L5-INTERNAL] setState(%u)\n", $x1
    continue
end

break ams::mitm::ldn::ICommunicationService::onNetworkChange
    commands
    silent
    printf "[ICOM-L5-INTERNAL] onNetworkChange(connected=%u)\n", $x2
    continue
end

break ams::mitm::ldn::ICommunicationService::onEventFired
    commands
    silent
    printf "[ICOM-L5-INTERNAL] onEventFired()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::SetWirelessControllerRestriction
    commands
    silent
    printf "[ICOM-L5-NYI] SetWirelessControllerRestriction()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::ScanPrivate
    commands
    silent
    printf "[ICOM-L5-NYI] ScanPrivate()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::CreateNetworkPrivate
    commands
    silent
    printf "[ICOM-L5-NYI] CreateNetworkPrivate()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::Reject
    commands
    silent
    printf "[ICOM-L5-NYI] Reject()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::AddAcceptFilterEntry
    commands
    silent
    printf "[ICOM-L5-NYI] AddAcceptFilterEntry()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::ClearAcceptFilter
    commands
    silent
    printf "[ICOM-L5-NYI] ClearAcceptFilter()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::ConnectPrivate
    commands
    silent
    printf "[ICOM-L5-NYI] ConnectPrivate()\n"
    continue
end

break ams::mitm::ldn::ICommunicationService::SetStationAcceptPolicy
    commands
    silent
    printf "[ICOM-L5-NYI] SetStationAcceptPolicy(%u)\n", $x1
    continue
end

break ams::mitm::ldn::ICommunicationService::InitializeSystem2
    commands
    silent
    printf "[ICOM-L5-NYI] InitializeSystem2(unk=0x%lx, PID=0x%lx)\n", $x1, $x2
    continue
end

echo [ICOM] Level 5 loaded (16 functions)\n
