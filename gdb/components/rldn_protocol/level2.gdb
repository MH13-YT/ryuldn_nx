# ==========================================
# RyuLDN Protocol - Level 2
# Lifecycle: initialization and finalization
# ==========================================

# Constructor
break ams::mitm::ldn::ryuldn::RyuLdnProtocol::RyuLdnProtocol
commands
silent
printf "[RLDN-PROTOCOL] Constructor called\n"
info locals
continue
end

# Destructor
break ams::mitm::ldn::ryuldn::RyuLdnProtocol::~RyuLdnProtocol
commands
silent
printf "[RLDN-PROTOCOL] Destructor called\n"
bt 2
continue
end

echo [RLDN-PROTOCOL] Level 2 (2 breakpoints with tracing)\n
