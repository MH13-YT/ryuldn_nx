#pragma once
// UPnP Client - Simple UPnP IGD Port Mapping
// Simplified version for Nintendo Switch (no Open.Nat dependency)

#include <vapours.hpp>
#include <stratosphere.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string>

namespace ams::mitm::ldn::ryuldn::proxy {

    // UPnP Protocol Types
    enum class UpnpProtocol : u8 {
        TCP = 0,
        UDP = 1
    };

    // Port Mapping Description
    struct PortMapping {
        UpnpProtocol protocol;
        u16 privatePort;
        u16 publicPort;
        u32 leaseDuration; // seconds
        std::string description;

        PortMapping()
            : protocol(UpnpProtocol::TCP),
              privatePort(0),
              publicPort(0),
              leaseDuration(3600),
              description("RyuLDN") {}

        PortMapping(UpnpProtocol proto, u16 privPort, u16 pubPort, u32 lease, const char* desc)
            : protocol(proto),
              privatePort(privPort),
              publicPort(pubPort),
              leaseDuration(lease),
              description(desc) {}
    };

    // Simple UPnP Client for Nintendo Switch
    class UpnpClient {
    private:
        static constexpr u16 SSDP_PORT = 1900;
        static constexpr const char* SSDP_MULTICAST = "239.255.255.250";
        static constexpr u32 DISCOVERY_TIMEOUT_MS = 2500;

        std::string _gatewayUrl;
        std::string _controlUrl;
        bool _discovered;

        s32 _socket;
        os::Mutex _mutex;

        bool SendSsdpDiscovery();
        bool ParseSsdpResponse(const char* response, size_t length);
        bool FetchDeviceDescription(const std::string& location);
        bool ParseControlUrl(const char* xml);

        bool SendSoapRequest(const std::string& action, const std::string& body, std::string& response);
        bool GetLocalIPAddress(std::string& ipAddress);

    public:
        UpnpClient();
        ~UpnpClient();

        // Discover UPnP IGD device
        bool DiscoverDevice();

        // Create port mapping
        bool CreatePortMapping(const PortMapping& mapping);

        // Delete port mapping
        bool DeletePortMapping(const PortMapping& mapping);

        // Get external IP address
        bool GetExternalIPAddress(std::string& ipAddress);

        // Check if device was discovered
        bool IsDiscovered() const { return _discovered; }

        // Get gateway URL
        const std::string& GetGatewayUrl() const { return _gatewayUrl; }
    };

} // namespace ams::mitm::ldn::ryuldn::proxy
