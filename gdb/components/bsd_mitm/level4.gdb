# ==========================================
# BSD Socket MITM - Level 4 (Cleanup)
# Close and internal helpers
# ==========================================

break ams::mitm::ldn::BsdMitmService::Close
    commands
    silent
    printf "[BSD-L4] Close(fd=%d) -> %d\n", $x2, $x0
    continue
end

break ams::mitm::ldn::BsdMitmService::GetSocketEntry
    commands
    silent
    printf "[BSD-L4-INTERNAL] GetSocketEntry(fd=%d)\n", $x1
    continue
end

break ams::mitm::ldn::BsdMitmService::IsRyuLdnVirtualIP
    commands
    silent
    printf "[BSD-L4-INTERNAL] IsRyuLdnVirtualIP(ip=0x%x) -> %d\n", $x1, $x0
    continue
end

echo [BSD] Level 4 loaded (3 functions)\n
