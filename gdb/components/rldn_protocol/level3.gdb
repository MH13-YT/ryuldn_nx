# ==========================================
# RyuLDN Protocol - Level 3
# Mode control and state transitions
# ==========================================

# Encoding methods
break ams::mitm::ldn::ryuldn::RyuLdnProtocol::EncodeHeader
commands
silent
printf "[RLDN-PROTOCOL] EncodeHeader(type=%d, size=%d)\n", $arg1, $arg2
continue
end

break ams::mitm::ldn::ryuldn::RyuLdnProtocol::Encode
commands
silent
printf "[RLDN-PROTOCOL] Encode() called\n"
bt 1
continue
end

echo [RLDN-PROTOCOL] Level 3 (2 breakpoints with tracing)\n
