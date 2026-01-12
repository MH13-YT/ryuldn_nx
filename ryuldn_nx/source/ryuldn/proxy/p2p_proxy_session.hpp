#pragma once
// P2P Proxy Session - Handles a single P2P client connection
// Matches Ryujinx LdnRyu/Proxy/P2pProxySession.cs

#include "../types.hpp"
#include "../ryu_ldn_protocol.hpp"
#include "../buffer_pool.hpp"
#include <stratosphere.hpp>
#include <sys/socket.h>

namespace ams::mitm::ldn::ryuldn::proxy {

    // Forward declaration
    class P2pProxyServer;

    class P2pProxySession {
    private:
        P2pProxyServer* _parent;
        s32 _socket;
        u32 _virtualIpAddress;
        bool _masterClosed;
        bool _running;

        // Use protocol with shared BufferPool (no permanent buffer)
        RyuLdnProtocol _protocol;

        os::ThreadType _receiveThread;
        std::unique_ptr<u8[]> _threadStack;
        static constexpr size_t ThreadStackSize = 0x4000;  // 16KB (increased for stability)

        // Receive buffer to avoid stack overflow during recv()
        std::unique_ptr<u8[]> _receiveBuffer;

        // Send mutex for thread-safe operations (NetCoreServer behavior)
        os::Mutex _sendMutex;

        static void ReceiveThreadFunc(void* arg);
        void ReceiveLoop();

        // Protocol handlers
        void HandleAuthentication(const LdnHeader& header, const ExternalProxyConfig& token);
        void HandleProxyDisconnect(const LdnHeader& header, const ProxyDisconnectMessageFull& message);
        void HandleProxyData(const LdnHeader& header, const ProxyDataHeaderFull& message, const u8* data, u32 dataSize);
        void HandleProxyConnectReply(const LdnHeader& header, const ProxyConnectResponseFull& data);
        void HandleProxyConnect(const LdnHeader& header, const ProxyConnectRequestFull& message);

    public:
        P2pProxySession(P2pProxyServer* server, s32 clientSocket);
        ~P2pProxySession();

        // Start session
        bool Start();

        // Stop session
        void Stop();

        // Disconnect and stop
        void DisconnectAndStop();

        // Reset session for reuse (for SessionPool)
        void Reset(P2pProxyServer* server, s32 clientSocket);

        // Set virtual IP address
        void SetIpv4(u32 ip) { _virtualIpAddress = ip; }

        // Get virtual IP address
        u32 GetVirtualIpAddress() const { return _virtualIpAddress; }

        // Get protocol
        RyuLdnProtocol* GetProtocol() { return &_protocol; }

        // Send data
        bool SendAsync(const u8* data, size_t size);

        // Get socket
        s32 GetSocket() const { return _socket; }
    };

} // namespace ams::mitm::ldn::ryuldn::proxy
