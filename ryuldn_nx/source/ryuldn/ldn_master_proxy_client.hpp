#pragma once

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

    // Callback pour les changements de réseau (correspond à la V1)
    using NetworkChangeCallback = std::function<void(const NetworkInfo&, bool)>;

    // Forward declarations comme dans la V1
    namespace proxy {
        class P2pProxyServer;
        class P2pProxyClient;
    }

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

        // Shared packet buffer
        std::unique_ptr<u8[]> _packetBuffer;
        os::Mutex _packetBufferMutex;

        // Événements rétablis en pointeurs bruts (non RAII)
        os::SystemEvent* _connectedEvent;
        os::SystemEvent* _errorEvent;
        os::SystemEvent* _scanEvent;
        os::SystemEvent* _rejectEvent;
        os::SystemEvent* _apConnectedEvent;

        RyuLdnProtocol _protocol;
        std::unique_ptr<NetworkTimeout> _timeout;

        std::vector<NetworkInfo> _availableGames;
        DisconnectReason _disconnectReason;
        NetworkError _lastError;
        NetworkInfo _lastNetworkInfo;

        InitializeMessage _initializeMemory;
        ProxyConfig _config;

        u8 _gameVersion[0x10];
        std::string _passphrase;

        // Proxies rétablis en pointeurs bruts (non RAII)
        proxy::P2pProxyServer* _hostedProxy;
        proxy::P2pProxyClient* _connectedProxy;

        std::mutex _sendMutex;

        // Callbacks
        NetworkChangeCallback _networkChangeCallback;
        ProxyConfigCallback _proxyConfigCallback;
        ProxyDataCallback _proxyDataCallback;

        // Méthodes privées
        static void WorkerThreadFunc(void* arg);
        void WorkerLoop();

        bool EnsureConnected();
        void Disconnect();
        void DisconnectInternal();
        void TimeoutConnection();

        int SendPacket(const u8* data, int size);
        int ReceiveData();

        void UpdatePassphraseIfNeeded(const char* passphrase);
        void ConfigureAccessPoint(RyuNetworkConfig& config);
        void DisconnectProxy();

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

        // Raw packet sending (Déplacé en PUBLIC pour correspondre à la V1)
        int SendRawPacket(const u8* data, int size);

        // Getters
        bool IsConnected() const { return _connected; }
        bool IsNetworkConnected() const { return _networkConnected; }
        const ProxyConfig& GetProxyConfig() const { return _config; }
        DisconnectReason GetDisconnectReason() const { return _disconnectReason; }
        RyuLdnProtocol* GetProtocol() { return &_protocol; }
    };

} // namespace ams::mitm::ldn::ryuldn
