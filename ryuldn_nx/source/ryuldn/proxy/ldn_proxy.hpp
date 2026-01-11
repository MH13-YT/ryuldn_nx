#pragma once
// LDN Proxy - Virtual Network Proxy
// Matches Ryujinx LdnRyu/Proxy/LdnProxy.cs
#include "../types.hpp"
#include "proxy_helpers.hpp"
#include "ephemeral_port_pool.hpp"
#include <stratosphere.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <vector>
#include <list>
#include <unordered_map>
#include <memory>

namespace ams::mitm::ldn::ryuldn {

    // Forward declaration
    class LdnMasterProxyClient;
    class RyuLdnProtocol;

    namespace proxy {

        // Forward declaration
        class LdnProxySocket;

        class LdnProxy {
        private:
            LdnMasterProxyClient* _parent;
            RyuLdnProtocol* _protocol;

            std::list<LdnProxySocket*> _sockets;
            os::Mutex _socketsMutex;

            std::unordered_map<s32, std::unique_ptr<EphemeralPortPool>> _ephemeralPorts; // keyed by protocol type

            // Shared packet buffer to avoid stack overflow
            std::unique_ptr<u8[]> _packetBuffer;
            os::Mutex _packetBufferMutex;

            u32 _subnetMask;
            u32 _localIp;
            u32 _broadcast;

            void RegisterHandlers(RyuLdnProtocol* protocol);
            void ForRoutedSockets(const ProxyInfo& info, std::function<void(LdnProxySocket*)> action);
            u32 GetIpV4(const sockaddr_in* endpoint);
            ProxyInfo MakeInfo(const sockaddr_in* localEp, const sockaddr_in* remoteEp, s32 protocolType);

        public:
            LdnProxy(const ProxyConfig& config, LdnMasterProxyClient* client, RyuLdnProtocol* protocol);
            ~LdnProxy();

            // Socket support
            bool Supported(s32 domain, s32 type, s32 protocol);

            // Port management
            u16 GetEphemeralPort(s32 protocolType);
            void ReturnEphemeralPort(s32 protocolType, u16 port);

            // Socket registration
            void RegisterSocket(LdnProxySocket* socket);
            void UnregisterSocket(LdnProxySocket* socket);

            // Protocol handlers (called by RyuLdnProtocol callbacks)
            void HandleConnectionRequest(const LdnHeader& header, const ProxyConnectRequestFull& request);
            void HandleConnectionResponse(const LdnHeader& header, const ProxyConnectResponseFull& response);
            void HandleData(const LdnHeader& header, const ProxyDataHeaderFull& proxyHeader, const u8* data, u32 dataSize);
            void HandleDisconnect(const LdnHeader& header, const ProxyDisconnectMessageFull& disconnect);

            // Connection management
            void RequestConnection(const sockaddr_in* localEp, const sockaddr_in* remoteEp, s32 protocolType);
            void SignalConnected(const sockaddr_in* localEp, const sockaddr_in* remoteEp, s32 protocolType);
            void EndConnection(const sockaddr_in* localEp, const sockaddr_in* remoteEp, s32 protocolType);

            // Data sending
            s32 SendTo(const u8* buffer, size_t bufferSize, s32 flags, const sockaddr_in* localEp, const sockaddr_in* remoteEp, s32 protocolType);

            // BSD socket compatibility methods (temporary)
            s32 SendTo(s32 fd, const u8* buffer, size_t bufferSize, const sockaddr_in* dest);
            Result RecvFrom(s32 fd, u8* buffer, size_t bufferSize, size_t* received, sockaddr_in* from);
            void CleanupSocket(s32 fd);

            // IP utilities
            u32 GetLocalIP() const { return _localIp; }
            bool IsBroadcast(u32 ip) const { return ip == _broadcast; }
            bool IsVirtualIP(u32 ip) const { return (ip & _subnetMask) == (_localIp & _subnetMask); }

            void UnregisterHandlers(RyuLdnProtocol* protocol);
            void Dispose();
        };

    } // namespace proxy

} // namespace ams::mitm::ldn::ryuldn
