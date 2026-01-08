#pragma once
// RyuLDN Protocol Handler
// Matches Ryujinx LdnRyu/RyuLdnProtocol.cs
#include "types.hpp"
#include <functional>
#include <memory>
#include <cstring>

namespace ams::mitm::ldn::ryuldn {

    // Callback types for received packets
    using InitializeCallback = std::function<void(const LdnHeader&, const InitializeMessage&)>;
    using PassphraseCallback = std::function<void(const LdnHeader&, const PassphraseMessage&)>;
    using NetworkInfoCallback = std::function<void(const LdnHeader&, const NetworkInfo&)>;
    using HeaderOnlyCallback = std::function<void(const LdnHeader&)>;
    using DisconnectCallback = std::function<void(const LdnHeader&, const DisconnectMessage&)>;
    using RejectCallback = std::function<void(const LdnHeader&, const RejectRequest&)>;
    using ScanFilterCallback = std::function<void(const LdnHeader&, const ScanFilter&)>;
    using ProxyConfigCallback = std::function<void(const LdnHeader&, const ProxyConfig&)>;
    using PingCallback = std::function<void(const LdnHeader&, const PingMessage&)>;
    using NetworkErrorCallback = std::function<void(const LdnHeader&, const NetworkErrorMessage&)>;
    using ExternalProxyCallback = std::function<void(const LdnHeader&, const ExternalProxyConfig&)>;
    using ExternalProxyTokenCallback = std::function<void(const LdnHeader&, const ExternalProxyToken&)>;
    using ExternalProxyStateCallback = std::function<void(const LdnHeader&, const ExternalProxyConnectionState&)>;
    using ProxyConnectCallback = std::function<void(const LdnHeader&, const ProxyConnectRequestFull&)>;
    using ProxyConnectReplyCallback = std::function<void(const LdnHeader&, const ProxyConnectResponseFull&)>;
    using ProxyDataCallback = std::function<void(const LdnHeader&, const ProxyDataHeaderFull&, const u8*, u32)>;
    using ProxyDisconnectCallback = std::function<void(const LdnHeader&, const ProxyDisconnectMessageFull&)>;

    class RyuLdnProtocol {
    private:
        static constexpr int HeaderSize = sizeof(LdnHeader);

        u8 _buffer[MaxPacketSize];
        int _bufferEnd;

        void DecodeAndHandle(const LdnHeader& header, const u8* data);

        template<typename T>
        static void ParseStruct(const u8* data, T& output) {
            std::memcpy(&output, data, sizeof(T));
        }

    public:
        RyuLdnProtocol();
        ~RyuLdnProtocol();

        void Reset();
        void Read(const u8* data, int offset, int size);

        // Callbacks for incoming packets - Client side
        InitializeCallback onInitialize;
        PassphraseCallback onPassphrase;
        NetworkInfoCallback onConnected;
        NetworkInfoCallback onSyncNetwork;
        NetworkInfoCallback onScanReply;
        HeaderOnlyCallback onScanReplyEnd;
        DisconnectCallback onDisconnected;

        // Callbacks for incoming packets - Server side
        HeaderOnlyCallback onRejectReply;

        // Proxy callbacks
        ProxyConfigCallback onProxyConfig;
        ExternalProxyCallback onExternalProxy;
        ExternalProxyTokenCallback onExternalProxyToken;
        ExternalProxyStateCallback onExternalProxyState;
        ProxyConnectCallback onProxyConnect;
        ProxyConnectReplyCallback onProxyConnectReply;
        ProxyDataCallback onProxyData;
        ProxyDisconnectCallback onProxyDisconnect;

        // Lifecycle callbacks
        PingCallback onPing;
        NetworkErrorCallback onNetworkError;

        // Encoding methods
        static void EncodeHeader(PacketId type, int dataSize, u8* output);

        static int Encode(PacketId type, u8* output);
        static int Encode(PacketId type, const u8* data, int dataSize, u8* output);

        template<typename T>
        static int Encode(PacketId type, const T& packet, u8* output) {
            LdnHeader header;
            header.magic = RyuLdnMagic;
            header.version = ProtocolVersion;
            header.type = static_cast<u8>(type);
            header.dataSize = sizeof(T);

            std::memcpy(output, &header, HeaderSize);
            std::memcpy(output + HeaderSize, &packet, sizeof(T));

            return HeaderSize + sizeof(T);
        }

        template<typename T>
        static int Encode(PacketId type, const T& packet, const u8* extraData, int extraDataSize, u8* output) {
            LdnHeader header;
            header.magic = RyuLdnMagic;
            header.version = ProtocolVersion;
            header.type = static_cast<u8>(type);
            header.dataSize = sizeof(T) + extraDataSize;

            std::memcpy(output, &header, HeaderSize);
            std::memcpy(output + HeaderSize, &packet, sizeof(T));
            std::memcpy(output + HeaderSize + sizeof(T), extraData, extraDataSize);

            return HeaderSize + sizeof(T) + extraDataSize;
        }
    };
}
