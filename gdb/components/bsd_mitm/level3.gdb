# ==========================================
# BSD Socket MITM - Level 3 (Data Transfer)
# Send/Recv operations
# ==========================================

break ams::mitm::ldn::BsdMitmService::Send
    commands
    silent
    printf "[BSD-L3] Send(fd=%d, size=%u, flags=%u) -> %d\n", $x2, $x3, $x4, $x0
    continue
end

break ams::mitm::ldn::BsdMitmService::SendTo
    commands
    silent
    printf "[BSD-L3] SendTo(fd=%d, size=%u, flags=%u) -> %d\n", $x2, $x3, $x4, $x0
    continue
end

break ams::mitm::ldn::BsdMitmService::Recv
    commands
    silent
    printf "[BSD-L3] Recv(fd=%d, size=%u, flags=%u) -> %d\n", $x2, $x3, $x4, $x0
    continue
end

break ams::mitm::ldn::BsdMitmService::RecvFrom
    commands
    silent
    printf "[BSD-L3] RecvFrom(fd=%d, size=%u, flags=%u) -> %d\n", $x2, $x3, $x4, $x0
    continue
end

echo [BSD] Level 3 loaded (4 functions)\n
