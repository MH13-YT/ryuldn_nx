#include "upnp_client.hpp"
#include "../../debug.hpp"
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

namespace ams::mitm::ldn::ryuldn::proxy {

        UpnpClient::UpnpClient()
                    : _discovered(false),
                        _lastHttpStatus(0),
                        _socket(-1),
                        _mutex(false),
                        _hasIgd(false)
    {
    }

    UpnpClient::~UpnpClient() {
        if (_socket >= 0) {
            close(_socket);
            _socket = -1;
        }
        if (_hasIgd) {
            FreeUPNPUrls(&_urls);
            _hasIgd = false;
        }
    }



    bool UpnpClient::DiscoverDevice() {
        std::scoped_lock lk(_mutex);

        LOG_INFO(COMP_RLDN_UPNP,"Starting device discovery (miniupnpc)...");

        int error = 0;
        /* 2000 ms timeout, default interface, no minissdpd socket, any local port, ipv4, ttl=2 */
        struct UPNPDev * devlist = upnpDiscover(2000, NULL, NULL, 0, 0, 2, &error);
        if (!devlist) {
            LOG_ERR_ARGS(COMP_RLDN_UPNP,"upnpDiscover failed: %d", error);
            return false;
        }

        char lanaddr[64];
        int igd = UPNP_GetValidIGD(devlist, &_urls, &_data, lanaddr, sizeof(lanaddr));
        freeUPNPDevlist(devlist);

        if (igd == 0) {
            LOG_ERR(COMP_RLDN_UPNP,"No valid IGD found");
            return false;
        }

        _hasIgd = true;
        _discovered = true;
        strncpy(_lanaddr, lanaddr, sizeof(_lanaddr)-1);
        _lanaddr[sizeof(_lanaddr)-1] = '\0';

        LOG_INFO_ARGS(COMP_RLDN_UPNP,"Found IGD controlURL=%s, service=%s", _urls.controlURL ? _urls.controlURL : "(nil)", _data.first.servicetype);
        return true;
    }



    bool UpnpClient::CreatePortMapping(const PortMapping& mapping) {
        std::scoped_lock lk(_mutex);
        if (!_discovered || !_hasIgd) {
            LOG_ERR(COMP_RLDN_UPNP,"Device not discovered, cannot create port mapping");
            return false;
        }

        LOG_INFO_ARGS(COMP_RLDN_UPNP,"CreatePortMapping - Protocol=%d, Private=%u, Public=%u, Lease=%u",
                 static_cast<int>(mapping.protocol),
                 mapping.privatePort,
                 mapping.publicPort,
                 mapping.leaseDuration);

        std::string localIP;
        if (!GetLocalIPAddress(localIP)) {
            LOG_ERR(COMP_RLDN_UPNP,"Failed to get local IP address, using LAN addr");
            localIP = std::string(_lanaddr);
        }

        char extPort[8];
        char inPort[8];
        char lease[16];
        snprintf(extPort, sizeof(extPort), "%u", mapping.publicPort);
        snprintf(inPort, sizeof(inPort), "%u", mapping.privatePort);
        snprintf(lease, sizeof(lease), "%u", mapping.leaseDuration);

        const char* proto = (mapping.protocol == UpnpProtocol::TCP) ? "TCP" : "UDP";
        const char* servicetype = _data.first.servicetype;
        const char* controlURL = _urls.controlURL;

        int r = UPNP_AddPortMapping(controlURL, servicetype,
                                    extPort, inPort,
                                    localIP.c_str(), mapping.description.c_str(),
                                    proto, NULL, lease);
        if (r != UPNPCOMMAND_SUCCESS) {
            LOG_ERR_ARGS(COMP_RLDN_UPNP,"UPNP_AddPortMapping failed: %d", r);
            return false;
        }
        return true;
    }

    bool UpnpClient::DeletePortMapping(const PortMapping& mapping) {
        std::scoped_lock lk(_mutex);
        if (!_discovered || !_hasIgd) return false;

        LOG_INFO_ARGS(COMP_RLDN_UPNP,"DeletePortMapping - Protocol=%d, Public=%u",
                 static_cast<int>(mapping.protocol),
                 mapping.publicPort);

        char extPort[8];
        snprintf(extPort, sizeof(extPort), "%u", mapping.publicPort);
        const char* proto = (mapping.protocol == UpnpProtocol::TCP) ? "TCP" : "UDP";
        const char* servicetype = _data.first.servicetype;
        const char* controlURL = _urls.controlURL;

        int r = UPNP_DeletePortMapping(controlURL, servicetype, extPort, proto, NULL);
        if (r != UPNPCOMMAND_SUCCESS) {
            LOG_ERR_ARGS(COMP_RLDN_UPNP,"UPNP_DeletePortMapping failed: %d", r);
            return false;
        }
        return true;
    }

    bool UpnpClient::GetExternalIPAddress([[maybe_unused]] std::string& ipAddress) {
        std::scoped_lock lk(_mutex);
        if (!_discovered || !_hasIgd) return false;

        char ip[40] = {0};
        int r = UPNP_GetExternalIPAddress(_urls.controlURL, _data.first.servicetype, ip);
        if (r != UPNPCOMMAND_SUCCESS) {
            LOG_ERR_ARGS(COMP_RLDN_UPNP,"UPNP_GetExternalIPAddress failed: %d", r);
            return false;
        }
        ipAddress = ip;
        return true;
    }
    bool UpnpClient::GetLocalIPAddress(std::string& ipAddress) {
        // Create UDP socket (doesn't actually send data)
        s32 sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            return false;
        }

        // Connect to a public IP (Google DNS) to determine local IP
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(80);
        inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            return false;
        }

        // Get local address
        struct sockaddr_in localAddr;
        socklen_t addrLen = sizeof(localAddr);
        if (getsockname(sock, (struct sockaddr*)&localAddr, &addrLen) < 0) {
            close(sock);
            return false;
        }

        close(sock);

        // Convert to string
        char ipStr[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &localAddr.sin_addr, ipStr, sizeof(ipStr))) {
            ipAddress.assign(ipStr);
            return true;
        }

        return false;
    }

} // namespace ams::mitm::ldn::ryuldn::proxy
