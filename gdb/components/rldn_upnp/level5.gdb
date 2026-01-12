# ==========================================
# RyuLDN UPnP - Level 5
# Complex operations and internals
# ==========================================

# Internal SOAP operations
break ams::mitm::ldn::ryuldn::proxy::UpnpClient::SendSoapRequest
break ams::mitm::ldn::ryuldn::proxy::UpnpClient::GetLocalIPAddress

# SSDP discovery
break ams::mitm::ldn::ryuldn::proxy::UpnpClient::SendSsdpDiscovery
break ams::mitm::ldn::ryuldn::proxy::UpnpClient::ParseSsdpResponse

echo [RLDN-UPNP] Level 5 (4 breakpoints)\n
