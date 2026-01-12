# ==========================================
# RyuLDN Protocol - Level 4
# Setup/teardown operations
# ==========================================

# Internal packet handling
break ams::mitm::ldn::ryuldn::RyuLdnProtocol::DecodeAndHandle
commands
silent
printf "[RLDN-PROTOCOL] DecodeAndHandle() - processing packet\n"
bt 2
info locals
continue
end

echo [RLDN-PROTOCOL] Level 4 (1 breakpoint with tracing)\n
