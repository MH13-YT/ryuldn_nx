# ==========================================
# RyuLDN Protocol - Level 5
# Complex operations and internals
# ==========================================

# Callback handlers (numerous)
break ams::mitm::ldn::ryuldn::RyuLdnProtocol::ParseStruct

# All callbacks are function pointers:
# onInitialize, onPassphrase, onConnected, onSyncNetwork, onScanReply,
# onScanReplyEnd, onDisconnected, onRejectReply, onProxyConfig,
# onExternalProxy, onExternalProxyToken, onExternalProxyState,
# onProxyConnect, onProxyConnectReply, onProxyData, onProxyDisconnect,
# onPing, onNetworkError

echo [RLDN-PROTOCOL] Level 5 (callback handlers)\n
