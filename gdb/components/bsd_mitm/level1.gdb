# ==========================================
# BSD Socket MITM - Level 1 (Socket Creation)
# Basic socket operations, no data transfer
# ==========================================

break ams::mitm::ldn::BsdMitmService::Socket
    commands
    silent
    printf "[BSD-L1] Socket(domain=%u, type=%u, proto=%u) -> fd=%d\n", $x1, $x2, $x3, $x0
    continue
end

echo [BSD] Level 1 loaded (1 function)\n
