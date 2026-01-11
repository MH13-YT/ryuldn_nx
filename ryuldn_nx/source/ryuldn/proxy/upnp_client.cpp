#include "upnp_client.hpp"
#include "../../debug.hpp"
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

namespace ams::mitm::ldn::ryuldn::proxy {

    UpnpClient::UpnpClient()
        : _discovered(false),
          _socket(-1),
          _mutex(false)
    {
    }

    UpnpClient::~UpnpClient() {
        if (_socket >= 0) {
            close(_socket);
            _socket = -1;
        }
    }

    bool UpnpClient::SendSsdpDiscovery() {
        // Create UDP socket
        _socket = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (_socket < 0) {
            LOG_ERR(COMP_RLDN_UPNP,"Failed to create socket");
            return false;
        }

        // Set socket timeout
        struct timeval tv;
        tv.tv_sec = DISCOVERY_TIMEOUT_MS / 1000;
        tv.tv_usec = (DISCOVERY_TIMEOUT_MS % 1000) * 1000;
        setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // SSDP M-SEARCH message
        const char* msearch =
            "M-SEARCH * HTTP/1.1\r\n"
            "HOST: 239.255.255.250:1900\r\n"
            "MAN: \"ssdp:discover\"\r\n"
            "MX: 2\r\n"
            "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n"
            "\r\n";

        // Send to SSDP multicast address
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(SSDP_PORT);
        inet_pton(AF_INET, SSDP_MULTICAST, &addr.sin_addr);

        ssize_t sent = sendto(_socket, msearch, strlen(msearch), 0,
                             (struct sockaddr*)&addr, sizeof(addr));

        if (sent < 0) {
            LOG_ERR(COMP_RLDN_UPNP,"Failed to send M-SEARCH");
            return false;
        }

        LOG_INFO(COMP_RLDN_UPNP,"Sent M-SEARCH discovery");
        return true;
    }

    bool UpnpClient::ParseSsdpResponse(const char* response, [[maybe_unused]] size_t length) {
        // Look for LOCATION header
        const char* location = strstr(response, "LOCATION:");
        if (!location) {
            location = strstr(response, "Location:");
        }

        if (location) {
            location += 9; // Skip "LOCATION:"
            while (*location == ' ') location++; // Skip spaces

            const char* end = strstr(location, "\r\n");
            if (end) {
                _gatewayUrl.assign(location, end - location);
                LOG_INFO_ARGS(COMP_RLDN_UPNP,"Found gateway at %s", _gatewayUrl.c_str());
                return FetchDeviceDescription(_gatewayUrl);
            }
        }

        return false;
    }

    bool UpnpClient::FetchDeviceDescription(const std::string& location) {
        // For now, simplified version - just extract control URL from location
        // In a full implementation, we would fetch and parse the device description XML

        // Extract base URL
        size_t protoEnd = location.find("://");
        if (protoEnd == std::string::npos) return false;

        size_t hostStart = protoEnd + 3;
        size_t pathStart = location.find('/', hostStart);

        if (pathStart == std::string::npos) {
            _controlUrl = location + "/ctl/IPConn";
        } else {
            _controlUrl = location.substr(0, pathStart) + "/ctl/IPConn";
        }

        LOG_INFO_ARGS(COMP_RLDN_UPNP,"Control URL: %s", _controlUrl.c_str());
        _discovered = true;
        return true;
    }

    bool UpnpClient::ParseControlUrl(const char* xml) {
        // Simplified XML parsing
        const char* controlUrl = strstr(xml, "<controlURL>");
        if (controlUrl) {
            controlUrl += 12;
            const char* end = strstr(controlUrl, "</controlURL>");
            if (end) {
                _controlUrl.assign(controlUrl, end - controlUrl);
                return true;
            }
        }
        return false;
    }

    bool UpnpClient::DiscoverDevice() {
        std::scoped_lock lk(_mutex);

        LOG_INFO(COMP_RLDN_UPNP,"Starting device discovery...");

        if (!SendSsdpDiscovery()) {
            return false;
        }

        // Wait for responses
        char buffer[2048];
        sockaddr_in from;
        socklen_t fromLen = sizeof(from);

        for (int i = 0; i < 5; i++) { // Try up to 5 responses
            ssize_t received = recvfrom(_socket, buffer, sizeof(buffer) - 1, 0,
                                       (struct sockaddr*)&from, &fromLen);

            if (received > 0) {
                buffer[received] = '\0';

                if (ParseSsdpResponse(buffer, received)) {
                    LOG_INFO(COMP_RLDN_UPNP,"Device discovered successfully");
                    return true;
                }
            } else if (received < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // Timeout
                }
            }
        }

        LOG_ERR(COMP_RLDN_UPNP,"No device found");
        return false;
    }

    bool UpnpClient::SendSoapRequest(const std::string& action, const std::string& body, std::string& response) {
        // Parse control URL: http://host:port/path
        size_t protoEnd = _controlUrl.find("://");
        if (protoEnd == std::string::npos) {
            LOG_ERR(COMP_RLDN_UPNP,"Invalid control URL");
            return false;
        }

        size_t hostStart = protoEnd + 3;
        size_t pathStart = _controlUrl.find('/', hostStart);
        size_t portStart = _controlUrl.find(':', hostStart);

        std::string host;
        std::string path = "/";
        int port = 80;

        if (portStart != std::string::npos && portStart < pathStart) {
            // Has port
            host = _controlUrl.substr(hostStart, portStart - hostStart);
            if (pathStart != std::string::npos) {
                port = std::stoi(_controlUrl.substr(portStart + 1, pathStart - portStart - 1));
                path = _controlUrl.substr(pathStart);
            } else {
                port = std::stoi(_controlUrl.substr(portStart + 1));
            }
        } else {
            // No port
            if (pathStart != std::string::npos) {
                host = _controlUrl.substr(hostStart, pathStart - hostStart);
                path = _controlUrl.substr(pathStart);
            } else {
                host = _controlUrl.substr(hostStart);
            }
        }

        LOG_INFO_ARGS(COMP_RLDN_UPNP,"Connecting to %s:%d%s", host.c_str(), port, path.c_str());

        // Create TCP socket
        s32 sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            LOG_ERR(COMP_RLDN_UPNP,"Failed to create TCP socket");
            return false;
        }

        // Set timeout
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        // Resolve and connect
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            LOG_ERR(COMP_RLDN_UPNP,"Invalid host address");
            close(sock);
            return false;
        }

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            LOG_ERR(COMP_RLDN_UPNP,"Connection failed");
            close(sock);
            return false;
        }

        // Build HTTP POST request
        std::string soapAction = "urn:schemas-upnp-org:service:WANIPConnection:1#" + action;
        char httpRequest[4096];
        snprintf(httpRequest, sizeof(httpRequest),
                "POST %s HTTP/1.1\r\n"
                "Host: %s:%d\r\n"
                "Content-Type: text/xml; charset=\"utf-8\"\r\n"
                "Content-Length: %zu\r\n"
                "SOAPAction: \"%s\"\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                path.c_str(),
                host.c_str(),
                port,
                body.length(),
                soapAction.c_str(),
                body.c_str());

        // Send request
        ssize_t sent = send(sock, httpRequest, strlen(httpRequest), 0);
        if (sent < 0) {
            LOG_ERR(COMP_RLDN_UPNP,"Failed to send HTTP request");
            close(sock);
            return false;
        }

        // Read response
        char respBuffer[4096];
        ssize_t received = recv(sock, respBuffer, sizeof(respBuffer) - 1, 0);
        close(sock);

        if (received <= 0) {
            LOG_ERR(COMP_RLDN_UPNP,"Failed to receive response");
            return false;
        }

        respBuffer[received] = '\0';
        response.assign(respBuffer, received);

        // Check for HTTP 200 OK
        if (strstr(respBuffer, "200 OK") != nullptr) {
            LOG_INFO_ARGS(COMP_RLDN_UPNP,"%s successful", action.c_str());
            return true;
        }

        LOG_INFO_ARGS(COMP_RLDN_UPNP,"%s failed - %s", action.c_str(), respBuffer);
        return false;
    }

    bool UpnpClient::CreatePortMapping(const PortMapping& mapping) {
        std::scoped_lock lk(_mutex);

        if (!_discovered) {
            LOG_ERR(COMP_RLDN_UPNP,"Device not discovered, cannot create port mapping");
            return false;
        }

        LOG_INFO_ARGS(COMP_RLDN_UPNP,"CreatePortMapping - Protocol=%d, Private=%u, Public=%u, Lease=%u",
                 static_cast<int>(mapping.protocol),
                 mapping.privatePort,
                 mapping.publicPort,
                 mapping.leaseDuration);

        // Get local IP address
        std::string localIP;
        if (!GetLocalIPAddress(localIP)) {
            LOG_ERR(COMP_RLDN_UPNP,"Failed to get local IP address");
            localIP = "192.168.1.100"; // Fallback
        }
        LOG_INFO_ARGS(COMP_RLDN_UPNP,"Using local IP: %s", localIP.c_str());

        // Build SOAP request body
        char soapBody[1024];
        snprintf(soapBody, sizeof(soapBody),
                "<?xml version=\"1.0\"?>"
                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                "<s:Body>"
                "<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
                "<NewRemoteHost></NewRemoteHost>"
                "<NewExternalPort>%u</NewExternalPort>"
                "<NewProtocol>%s</NewProtocol>"
                "<NewInternalPort>%u</NewInternalPort>"
                "<NewInternalClient>%s</NewInternalClient>"
                "<NewEnabled>1</NewEnabled>"
                "<NewPortMappingDescription>%s</NewPortMappingDescription>"
                "<NewLeaseDuration>%u</NewLeaseDuration>"
                "</u:AddPortMapping>"
                "</s:Body>"
                "</s:Envelope>",
                mapping.publicPort,
                mapping.protocol == UpnpProtocol::TCP ? "TCP" : "UDP",
                mapping.privatePort,
                localIP.c_str(),
                mapping.description.c_str(),
                mapping.leaseDuration);

        std::string response;
        return SendSoapRequest("AddPortMapping", soapBody, response);
    }

    bool UpnpClient::DeletePortMapping(const PortMapping& mapping) {
        std::scoped_lock lk(_mutex);

        if (!_discovered) {
            return false;
        }

        LOG_INFO_ARGS(COMP_RLDN_UPNP,"DeletePortMapping - Protocol=%d, Public=%u",
                 static_cast<int>(mapping.protocol),
                 mapping.publicPort);

        // Build SOAP request (simplified)
        char soapBody[512];
        snprintf(soapBody, sizeof(soapBody),
                "<?xml version=\"1.0\"?>"
                "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                "<s:Body>"
                "<u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
                "<NewRemoteHost></NewRemoteHost>"
                "<NewExternalPort>%u</NewExternalPort>"
                "<NewProtocol>%s</NewProtocol>"
                "</u:DeletePortMapping>"
                "</s:Body>"
                "</s:Envelope>",
                mapping.publicPort,
                mapping.protocol == UpnpProtocol::TCP ? "TCP" : "UDP");

        std::string response;
        return SendSoapRequest("DeletePortMapping", soapBody, response);
    }

    bool UpnpClient::GetExternalIPAddress([[maybe_unused]] std::string& ipAddress) {
        std::scoped_lock lk(_mutex);

        if (!_discovered) {
            return false;
        }

        const char* soapBody =
            "<?xml version=\"1.0\"?>"
            "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
            "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
            "<s:Body>"
            "<u:GetExternalIPAddress xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">"
            "</u:GetExternalIPAddress>"
            "</s:Body>"
            "</s:Envelope>";

        std::string response;
        if (SendSoapRequest("GetExternalIPAddress", soapBody, response)) {
            // Parse IP from response
            // TODO: XML parsing
            return true;
        }

        return false;
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
