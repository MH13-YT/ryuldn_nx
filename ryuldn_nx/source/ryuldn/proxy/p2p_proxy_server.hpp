#pragma once
// P2P Proxy Server - Main P2P relay server
// Matches Ryujinx LdnRyu/Proxy/P2pProxyServer.cs
// Manages P2P client connections and UPnP port mapping

#include "../types.hpp"
#include "../ryu_ldn_protocol.hpp"
#include "p2p_proxy_session.hpp"
#include "upnp_client.hpp"
#include <stratosphere.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <list>
#include <vector>

namespace ams::mitm::ldn::ryuldn {
    // Forward declaration
    class LdnMasterProxyClient;
}

namespace ams::mitm::ldn::ryuldn::proxy {

    class P2pProxyServer {
    public:
        static constexpr u16 PrivatePortBase = 39990;
        static constexpr s32 PrivatePortRange = 10;

    private:
        static constexpr u16 PublicPortBase = 39990;
        static constexpr s32 PublicPortRange = 10;
        static constexpr u32 PortLeaseLength = 60; // seconds
        static constexpr u32 PortLeaseRenew = 50;  // seconds
        static constexpr u32 AuthWaitSeconds = 1;

        // Server state
        u16 _privatePort;
        u16 _publicPort;
        s32 _listenSocket;
        bool _running;
        bool _disposed;

        // Network configuration
        u32 _broadcastAddress;

        // UPnP
        UpnpClient _upnpClient;
        PortMapping _portMapping;
        bool _hasPortMapping;

        // Master server connection
        LdnMasterProxyClient* _master;
        RyuLdnProtocol* _masterProtocol;
        RyuLdnProtocol _protocol;

        // Sessions
        std::list<P2pProxySession*> _players;
        os::Mutex _playersLock;

        // Authentication tokens
        std::vector<ExternalProxyToken> _waitingTokens;
        os::Mutex _tokensLock;
        os::SystemEvent _tokenEvent;

        // Port lease renewal
        os::ThreadType _leaseThread;
        std::unique_ptr<u8[]> _leaseThreadStack;
        bool _leaseThreadRunning;
        static constexpr size_t LeaseThreadStackSize = 0x4000;

        // Accept thread
        os::ThreadType _acceptThread;
        std::unique_ptr<u8[]> _acceptThreadStack;
        static constexpr size_t AcceptThreadStackSize = 0x4000;

        // Thread functions
        static void AcceptThreadFunc(void* arg);
        static void LeaseRenewalThreadFunc(void* arg);
        void AcceptLoop();
        void LeaseRenewalLoop();

        // Protocol event handlers
        void HandleToken(const LdnHeader& header, const ExternalProxyToken& token);
        void HandleStateChange(const LdnHeader& header, const ExternalProxyConnectionState& state);

        // Message routing
        template<typename TMessage>
        void RouteMessage(P2pProxySession* sender, TMessage& message,
                         std::function<void(P2pProxySession*)> action);

        // Port mapping
        bool RefreshLease();

    public:
        P2pProxyServer(LdnMasterProxyClient* master, u16 port, RyuLdnProtocol* masterProtocol);
        ~P2pProxyServer();

        // Server control
        bool Start();
        void Stop();
        void Dispose();

        // Configuration
        void Configure(const ProxyConfig& config);

        // UPnP NAT punch
        u16 NatPunch();

        // User registration (called by P2pProxySession during authentication)
        bool TryRegisterUser(P2pProxySession* session, const ExternalProxyConfig& config);

        // Client disconnection (called by P2pProxySession)
        void DisconnectProxyClient(P2pProxySession* session);

        // Proxy message handlers (called by P2pProxySession)
        void HandleProxyDisconnect(P2pProxySession* sender, const LdnHeader& header, const ProxyDisconnectMessageFull& message);
        void HandleProxyData(P2pProxySession* sender, const LdnHeader& header, const ProxyDataHeaderFull& message, const u8* data, u32 dataSize);
        void HandleProxyConnectReply(P2pProxySession* sender, const LdnHeader& header, const ProxyConnectResponseFull& message);
        void HandleProxyConnect(P2pProxySession* sender, const LdnHeader& header, const ProxyConnectRequestFull& message);

        // Accessors
        u16 GetPrivatePort() const { return _privatePort; }
        u16 GetPublicPort() const { return _publicPort; }
        bool IsRunning() const { return _running; }
        RyuLdnProtocol* GetProtocol() { return &_protocol; }
        LdnMasterProxyClient* GetMaster() { return _master; }
        RyuLdnProtocol* GetMasterProtocol() { return _masterProtocol; }
    };

} // namespace ams::mitm::ldn::ryuldn::proxy
