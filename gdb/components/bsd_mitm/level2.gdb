# ==========================================
# BSD Socket MITM - Level 2 (Connection)
# Bind and Connect operations
# ==========================================

break ams::mitm::ldn::BsdMitmService::Bind
    commands
    silent
    printf "[BSD-L2] Bind(fd=%d)\n", $x2
    continue
end

break ams::mitm::ldn::BsdMitmService::Connect
    commands
    silent
    printf "[BSD-L2] Connect(fd=%d)\n", $x2
    continue
end

echo [BSD] Level 2 loaded (2 functions)\n
