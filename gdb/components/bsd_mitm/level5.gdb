# ==========================================
# BSD Socket MITM - Level 5 (Static/Lifecycle)
# Static methods, constructor/destructor
# ==========================================

break ams::mitm::ldn::BsdMitmService::ShouldMitm
    commands
    silent
    printf "[BSD-L5] ShouldMitm(pid=0x%lx) -> %d\n", $x1, $x0
    continue
end

break ams::mitm::ldn::BsdMitmService::RegisterProxy
    commands
    silent
    printf "[BSD-L5] RegisterProxy(proxy=%p)\n", $x0
    continue
end

break ams::mitm::ldn::BsdMitmService::UnregisterProxy
    commands
    silent
    printf "[BSD-L5] UnregisterProxy()\n"
    continue
end

break ams::mitm::ldn::BsdMitmService::BsdMitmService
    commands
    silent
    printf "[BSD-L5] Constructor()\n"
    continue
end

break ams::mitm::ldn::BsdMitmService::~BsdMitmService
    commands
    silent
    printf "[BSD-L5] Destructor()\n"
    continue
end

echo [BSD] Level 5 loaded (5 functions)\n
