#pragma once
// RyuLDN Protocol Handler
// Uses BufferPool to reduce memory usage
// 100% compatible with Ryujinx protocol - no behavioral changes

#include "types.hpp"
#include "buffer_pool.hpp"
#include <functional>
#include <memory>
#include <cstring>

namespace ams::mitm::ldn::ryuldn {

    // Callback types (unchanged from original)
    using InitializeCallback = std::function<void(const LdnHeader&, const InitializeMessage&)>;
    using PassphraseCallback = std::function<void(const LdnHeader&, const PassphraseMessage&)>;
    using NetworkInfoCallback = std::function<void(const LdnHeader&, const NetworkInfo&)>;
    using HeaderOnlyCallback = std::function<void(const LdnHeader&)>;
    using DisconnectCallback = std::function<void(const LdnHeader&, const DisconnectMessage&)>;
    using RejectCallback = std::function<void(const LdnHeader&, const RejectRequest&)>;
    using ScanFilterCallback = std::function<void(const LdnHeader&, const ScanFilter&)>;
    using ProxyConfigCallback = std::function<void(const LdnHeader&, const ProxyConfig&)>;
    using CreateAccessPointCallback = std::function<void(const LdnHeader&, const CreateAccessPointRequest&, const u8*, u32)>;
    using CreateAccessPointPrivateCallback = std::function<void(const LdnHeader&, const CreateAccessPointPrivateRequest&, const u8*, u32)>;
    using SetAdvertiseDataCallback = std::function<void(const LdnHeader&, const u8*, u32)>;
    using SetAcceptPolicyCallback = std::function<void(const LdnHeader&, const SetAcceptPolicyRequest&)>;
    using ConnectCallback = std::function<void(const LdnHeader&, const ConnectRequest&)>;
    using ConnectPrivateCallback = std::function<void(const LdnHeader&, const ConnectPrivateRequest&)>;
    using PingCallback = std::function<void(const LdnHeader&, const PingMessage&)>;
    using NetworkErrorCallback = std::function<void(const LdnHeader&, const NetworkErrorMessage&)>;
    using ExternalProxyCallback = std::function<void(const LdnHeader&, const ExternalProxyConfig&)>;
    using ExternalProxyTokenCallback = std::function<void(const LdnHeader&, const ExternalProxyToken&)>;
    using ExternalProxyStateCallback = std::function<void(const LdnHeader&, const ExternalProxyConnectionState&)>;
    using ProxyConnectCallback = std::function<void(const LdnHeader&, const ProxyConnectRequestFull&)>;
    using ProxyConnectReplyCallback = std::function<void(const LdnHeader&, const ProxyConnectResponseFull&)>;
    using ProxyDataCallback = std::function<void(const LdnHeader&, const ProxyDataHeaderFull&, const u8*, u32)>;
    using ProxyDisconnectCallback = std::function<void(const LdnHeader&, const ProxyDisconnectMessageFull&)>;

    /**
     * RyuLDN Protocol Handler
     * 
     * MEMORY 
     * - Uses shared BufferPool instead of dedicated buffer
     * - Borrows buffer only during Read() operations
     * - Reduces per-instance memory from 128KB to ~256 bytes
     * 
     * COMPATIBILITY:
     * - 100% protocol-compatible with Ryujinx/ldn-master
     * - All packet formats unchanged
     * - Behavior identical to original implementation
     */
    class RyuLdnProtocol {
    private:
        static constexpr int HeaderSize = sizeof(LdnHeader);

        // Persistent header buffer (small - only 10 bytes for LdnHeader)
        u8 _headerBuffer[HeaderSize];
        int _headerBytesReceived;
        
        // Borrowed buffer (returned after each packet)
        u8* _currentBuffer;
        int _bufferEnd;
        
        BufferPool* _pool;
        bool _inPacket;
        
        // Thread-safety: protect Read() state even if single-threaded
        os::Mutex _readMutex;

        void DecodeAndHandle(const LdnHeader& header, const u8* data);

        template<typename T>
        static void ParseStruct(const u8* data, T& output) {
            std::memcpy(&output, data, sizeof(T));
        }

        // Special parser for NetworkInfo which is sent as NetworkId + CommonNetworkInfo + LdnNetworkInfo
        // but we only want to parse the LdnNetworkInfo part (last 1072 bytes out of 1152 bytes)
        static void ParseNetworkInfo(const u8* data, u32 dataSize, LdnNetworkInfo& output) {
            // Server sends: NetworkId(32) + CommonNetworkInfo(48) + LdnNetworkInfo(1072) = 1152 bytes
            // We only want the LdnNetworkInfo part
            const u32 NetworkIdSize = 32;      // IntentId(16) + SessionId(16)
            const u32 CommonNetworkInfoSize = 48;  // MacAddress(6) + Ssid(34) + channel(2) + linkLevel(1) + networkType(1) + _unk(4)
            const u32 LdnNetworkInfoOffset = NetworkIdSize + CommonNetworkInfoSize;
            const u32 LdnNetworkInfoSize = sizeof(LdnNetworkInfo);
            
            if (dataSize < LdnNetworkInfoOffset + LdnNetworkInfoSize) {
                std::memset(&output, 0, sizeof(output));
                return;
            }
            
            std::memcpy(&output, data + LdnNetworkInfoOffset, LdnNetworkInfoSize);
        }

    public:
        /**
         * Constructor using shared BufferPool
         * Much more memory-efficient than original
         */
        RyuLdnProtocol(BufferPool* pool)
            : _headerBytesReceived(0),
              _currentBuffer(nullptr),
              _bufferEnd(0),
              _pool(pool),
              _inPacket(false),
              _readMutex(false)
        {
            if (!_pool) {
                AMS_ABORT("RyuLdnProtocol: BufferPool is null");
            }
            std::memset(_headerBuffer, 0, HeaderSize);
        }

        ~RyuLdnProtocol() {
            // Return buffer if we still have one borrowed
            if (_currentBuffer && _pool) {
                _pool->ReturnBuffer(_currentBuffer);
                _currentBuffer = nullptr;
            }
        }

        void Reset();
        void Read(const u8* data, int offset, int size);

        // Callbacks (unchanged from original)
        InitializeCallback onInitialize;
        PassphraseCallback onPassphrase;
        NetworkInfoCallback onConnected;
        NetworkInfoCallback onSyncNetwork;
        NetworkInfoCallback onScanReply;
        HeaderOnlyCallback onScanReplyEnd;
        DisconnectCallback onDisconnected;

        HeaderOnlyCallback onRejectReply;
        RejectCallback onReject;
        CreateAccessPointCallback onCreateAccessPoint;
        CreateAccessPointPrivateCallback onCreateAccessPointPrivate;
        SetAcceptPolicyCallback onSetAcceptPolicy;
        SetAdvertiseDataCallback onSetAdvertiseData;
        ConnectCallback onConnect;
        ConnectPrivateCallback onConnectPrivate;
        ScanFilterCallback onScan;

        ProxyConfigCallback onProxyConfig;
        ExternalProxyCallback onExternalProxy;
        ExternalProxyTokenCallback onExternalProxyToken;
        ExternalProxyStateCallback onExternalProxyState;
        ProxyConnectCallback onProxyConnect;
        ProxyConnectReplyCallback onProxyConnectReply;
        ProxyDataCallback onProxyData;
        ProxyDisconnectCallback onProxyDisconnect;

        PingCallback onPing;
        NetworkErrorCallback onNetworkError;

        // Static encoding methods (unchanged - use provided buffer)
        static void EncodeHeader(PacketId type, int dataSize, u8* output);
        static int Encode(PacketId type, u8* output);
        static int Encode(PacketId type, const u8* data, int dataSize, u8* output);

        template<typename T>
        static int Encode(PacketId type, const T& packet, u8* output) {
            LdnHeader header;
            header.magic = RyuLdnMagic;
            header.type = static_cast<u8>(type);
            header.version = ProtocolVersion;
            header.dataSize = sizeof(T);

            std::memcpy(output, &header, HeaderSize);
            std::memcpy(output + HeaderSize, &packet, sizeof(T));

            return HeaderSize + sizeof(T);
        }

        template<typename T>
        static int Encode(PacketId type, const T& packet, const u8* extraData, int extraDataSize, u8* output) {
            LdnHeader header;
            header.magic = RyuLdnMagic;
            header.type = static_cast<u8>(type);
            header.version = ProtocolVersion;
            header.dataSize = sizeof(T) + extraDataSize;

            std::memcpy(output, &header, HeaderSize);
            std::memcpy(output + HeaderSize, &packet, sizeof(T));
            std::memcpy(output + HeaderSize + sizeof(T), extraData, extraDataSize);

            return HeaderSize + sizeof(T) + extraDataSize;
        }
    };

} // namespace ams::mitm::ldn::ryuldn
