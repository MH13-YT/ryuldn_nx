# ==========================================
# RyuLDN UPnP - Level 2
# UPnP discovery and setup
# ==========================================

# Constructor/Destructor
break ams::mitm::ldn::ryuldn::proxy::UpnpClient::UpnpClient
break ams::mitm::ldn::ryuldn::proxy::UpnpClient::~UpnpClient

# Discovery
break ams::mitm::ldn::ryuldn::proxy::UpnpClient::DiscoverDevice

echo [RLDN-UPNP] Level 2 (3 breakpoints)\n
