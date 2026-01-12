#pragma once
// P2P Proxy Client - Client connection to P2P relay server
// Matches Ryujinx LdnRyu/Proxy/P2pProxyClient.cs
// Connects to P2P server and receives proxy configuration

#include "../types.hpp"
#include "../ryu_ldn_protocol.hpp"
#include "../buffer_pool.hpp"
#include <stratosphere.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string>

namespace ams::mitm::ldn::ryuldn::proxy {

    // Forward declaration
    class LdnProxy;

    class P2pProxyClient {
    private:
        static constexpr u32 FailureTimeoutMs = 4000;
        static constexpr size_t ReceiveBufferSize = 8192;

        std::string _address;
        u16 _port;
        s32 _socket;
        bool _connected;
        bool _ready;
        bool _running;

        ProxyConfig _proxyConfig;
        
        // Use protocol with shared BufferPool
        RyuLdnProtocol _protocol;

        // Thread management
        os::ThreadType _receiveThread;
        std::unique_ptr<u8[]> _threadStack;
        static constexpr size_t ThreadStackSize = 0x4000;  // 16KB (increased from 12KB for stability)
        
        // Synchronization
        os::SystemEvent _connectedEvent;
        os::SystemEvent _readyEvent;
        os::Mutex _stateMutex;
        os::Mutex _sendMutex;  // Thread-safe send (NetCoreServer behavior)

        // Protocol handlers
        void HandleProxyConfig(const LdnHeader& header, const ProxyConfig& config);

        // Thread functions
        static void ReceiveThreadFunc(void* arg);
        void ReceiveLoop();

    public:
        P2pProxyClient(const std::string& address, u16 port);
        ~P2pProxyClient();

        // Connect to server
        bool Connect();

        // Disconnect from server
        void Disconnect();

        // Perform authentication
        bool PerformAuth(const ExternalProxyConfig& config);

        // Wait for proxy to be ready
        bool EnsureProxyReady();

        // Send data
        bool SendAsync(const u8* data, size_t size);

        // Accessors
        bool IsConnected() const { return _connected; }
        bool IsReady() const { return _ready; }
        const ProxyConfig& GetProxyConfig() const { return _proxyConfig; }
        RyuLdnProtocol* GetProtocol() { return &_protocol; }
    };

} // namespace ams::mitm::ldn::ryuldn::proxy
