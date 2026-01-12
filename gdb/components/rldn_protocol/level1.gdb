# ==========================================
# RyuLDN Protocol - Level 1
# Simple protocol queries, no side effects
# ==========================================

# Query functions
break ams::mitm::ldn::ryuldn::RyuLdnProtocol::Reset
commands
silent
printf "[RLDN-PROTOCOL] Reset()\n"
bt 1
continue
end

break ams::mitm::ldn::ryuldn::RyuLdnProtocol::Read
commands
silent
printf "[RLDN-PROTOCOL] Read(offset=%d, size=%d)\n", $arg2, $arg3
bt 1
continue
end

echo [RLDN-PROTOCOL] Level 1 (2 breakpoints with tracing)\n
