#pragma once
// LDN Master Proxy Client
// Matches Ryujinx LdnRyu/LdnMasterProxyClient.cs
#include "ryu_ldn_protocol.hpp"
#include "network_timeout.hpp"
#include "types.hpp"
#include "proxy/p2p_proxy_server.hpp"
#include "proxy/p2p_proxy_client.hpp"
#include <stratosphere.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>

namespace ams::mitm::ldn::ryuldn {

    // Callback for network changes (not defined in protocol.hpp)
    using NetworkChangeCallback = std::function<void(const NetworkInfo&, bool)>;
    // Note: ProxyConfigCallback and ProxyDataCallback are already defined in ryu_ldn_protocol.hpp

    // Forward declarations
    namespace proxy {
        class P2pProxyServer;
        class P2pProxyClient;
    }

    // Main RyuLDN client for connecting to master server
    // Matches Ryujinx LdnRyu/LdnMasterProxyClient.cs
    class LdnMasterProxyClient {
    private:
        std::string _serverAddress;
        int _serverPort;

        int _socket;
        bool _connected;
        bool _networkConnected;
        bool _stop;
        bool _useP2pProxy;

        os::ThreadType _workerThread;
        std::unique_ptr<u8[]> _threadStack;
        static constexpr size_t ThreadStackSize = 0x8000;

        os::SystemEvent* _connectedEvent;
        os::SystemEvent* _errorEvent;
        os::SystemEvent* _scanEvent;
        os::SystemEvent* _rejectEvent;
        os::SystemEvent* _apConnectedEvent;

        RyuLdnProtocol _protocol;

        std::vector<NetworkInfo> _availableGames;
        DisconnectReason _disconnectReason;
        NetworkError _lastError;

        InitializeMessage _initializeMemory;
        ProxyConfig _config;

        u8 _gameVersion[0x10];
        std::string _passphrase;

        // P2P Proxy support
        proxy::P2pProxyServer* _hostedProxy;
        proxy::P2pProxyClient* _connectedProxy;

        std::mutex _sendMutex;

        // Callbacks
        NetworkChangeCallback _networkChangeCallback;
        ProxyConfigCallback _proxyConfigCallback;
        ProxyDataCallback _proxyDataCallback;

        // Private methods
        static void WorkerThreadFunc(void* arg);
        void WorkerLoop();

        bool EnsureConnected();
        void Disconnect();
        void DisconnectInternal();

        int SendPacket(const u8* data, int size);
        int ReceiveData();

        void UpdatePassphraseIfNeeded(const char* passphrase);

        // Protocol event handlers
        void HandleInitialize(const LdnHeader& header, const InitializeMessage& msg);
        void HandleConnected(const LdnHeader& header, const NetworkInfo& info);
        void HandleSyncNetwork(const LdnHeader& header, const NetworkInfo& info);
        void HandleDisconnected(const LdnHeader& header, const DisconnectMessage& msg);
        void HandleRejectReply(const LdnHeader& header);
        void HandleScanReply(const LdnHeader& header, const NetworkInfo& info);
        void HandleScanReplyEnd(const LdnHeader& header);
        void HandleProxyConfig(const LdnHeader& header, const ProxyConfig& config);
        void HandleProxyData(const LdnHeader& header, const ProxyDataHeaderFull& hdr, const u8* payload, u32 payloadSize);
        void HandlePing(const LdnHeader& header, const PingMessage& ping);
        void HandleNetworkError(const LdnHeader& header, const NetworkErrorMessage& error);
        void HandleExternalProxy(const LdnHeader& header, const ExternalProxyConfig& config);

        NetworkError ConsumeNetworkError();

    public:
        LdnMasterProxyClient(const char* serverAddress, int serverPort, bool useP2pProxy = true);
        ~LdnMasterProxyClient();

        Result Initialize();
        Result Finalize();

        void SetNetworkChangeCallback(NetworkChangeCallback callback);
        void SetProxyConfigCallback(ProxyConfigCallback callback);
        void SetProxyDataCallback(ProxyDataCallback callback);
        void SetGameVersion(const u8* version, size_t size);
        void SetPassphrase(const char* passphrase);

        // LDN operations
        Result CreateNetwork(const CreateAccessPointRequest& request, const u8* advertiseData, u16 advertiseDataSize);
        Result CreateNetworkPrivate(const CreateAccessPointPrivateRequest& request, const u8* advertiseData, u16 advertiseDataSize);

        Result Connect(const ConnectRequest& request);
        Result ConnectPrivate(const ConnectPrivateRequest& request);

        Result DisconnectNetwork();
        Result Scan(NetworkInfo* networks, u16* count, const ScanFilter& filter);

        Result SetAdvertiseData(const u8* data, u16 size);
        Result SetStationAcceptPolicy(u8 acceptPolicy);
        Result Reject(DisconnectReason reason, u32 nodeId);

        // Raw packet sending (for proxy)
        int SendRawPacket(const u8* data, int size);

        // Getters
        bool IsConnected() const { return _connected; }
        bool IsNetworkConnected() const { return _networkConnected; }
        const ProxyConfig& GetProxyConfig() const { return _config; }
        DisconnectReason GetDisconnectReason() const { return _disconnectReason; }
        RyuLdnProtocol* GetProtocol() { return &_protocol; }
    };

} // namespace ams::mitm::ldn::ryuldn
