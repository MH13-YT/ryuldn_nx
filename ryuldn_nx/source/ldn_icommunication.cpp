#include "ldn_icommunication.hpp"
#include "bsd_mitm_service.hpp"
#include <arpa/inet.h>
#include <cstring>

namespace ams::mitm::ldn {
    static_assert(sizeof(NetworkInfo) == 0x480, "sizeof(NetworkInfo) should be 0x480");
    static_assert(sizeof(ConnectNetworkData) == 0x7C, "sizeof(ConnectNetworkData) should be 0x7C");
    static_assert(sizeof(ScanFilter) == 0x60, "sizeof(ScanFilter) should be 0x60");

    // ----- Sélection du serveur RyuLDN -----

    enum class RyuLdnProfile : u8 {
        Official = 0,
        Custom   = 1,
        Count
    };

    // Hardcode le profil actif ici :
    static constexpr RyuLdnProfile RYULDN_ACTIVE_PROFILE = RyuLdnProfile::Custom;

    struct RyuLdnServerConfig {
        const char* address;
        int         port;
    };

    static constexpr RyuLdnServerConfig RYULDN_PROFILES[] = {
        /* Official */ { "ldn.ryujinx.app", 30456 },
        /* Custom   */ { "192.168.1.186",   30456 },
    };

    static constexpr const RyuLdnServerConfig& GetActiveRyuLdnConfig() {
        return RYULDN_PROFILES[static_cast<u8>(RYULDN_ACTIVE_PROFILE)];
    }

    static const RyuLdnServerConfig& g_RyuLdnConfig = GetActiveRyuLdnConfig();

    const char* ICommunicationService::DisconnectReasonToString(u32 reason) {
        switch (reason) {
            case 0: return "None";
            case 1: return "DisconnectedByUser";
            case 2: return "DisconnectedBySystem";
            case 3: return "DestroyedByUser";
            case 4: return "DestroyedBySystem";
            case 5: return "Rejected";
            case 6: return "SignalLost";
            default: return "Unknown";
        }
    }

    void ICommunicationService::setState(CommState state) {
        current_state = state;
        onEventFired();
    }

    void ICommunicationService::onNetworkChange(const NetworkInfo& info, bool connected) {
        if (connected) {
            network_info = info;
            if (current_state == CommState::AccessPoint) {
                LOG_INFO(COMP_LDN_ICOM, "onNetworkChange: AP created, state AccessPoint -> AccessPointCreated");
                setState(CommState::AccessPointCreated);
            } else if (current_state == CommState::Station) {
                LOG_INFO(COMP_LDN_ICOM, "onNetworkChange: Station connected, state Station -> StationConnected");
                setState(CommState::StationConnected);
            }
        } else {
            if (current_state == CommState::AccessPointCreated) {
                LOG_INFO(COMP_LDN_ICOM, "onNetworkChange: AP disconnected, state AccessPointCreated -> AccessPoint");
                setState(CommState::AccessPoint);
            } else if (current_state == CommState::StationConnected) {
                LOG_INFO(COMP_LDN_ICOM, "onNetworkChange: Station disconnected, state StationConnected -> Station");
                setState(CommState::Station);
            }
        }
    }

    void ICommunicationService::onEventFired() {
        if (this->state_event) {
            LOG_INFO(COMP_LDN_ICOM, "onEventFired signal_event");
            this->state_event->Signal();
        }
    }

    Result ICommunicationService::Initialize(const sf::ClientProcessId &client_process_id) {
        LOG_INFO_ARGS(COMP_LDN_ICOM, "ICommunicationService::Initialize pid: %" PRIu64, client_process_id.GetValue());

        if (this->state_event == nullptr) {
            // ClearMode, inter_process
            LOG_INFO(COMP_LDN_ICOM, "StateEvent is null");
            LOG_HEAP(COMP_LDN_ICOM, "before SystemEvent");
            this->state_event = new (std::nothrow) os::SystemEvent(::ams::os::EventClearMode_AutoClear, true);

            if (this->state_event == nullptr) {
                LOG_INFO(COMP_LDN_ICOM, "ERROR: Failed to allocate SystemEvent - out of memory");
                return MAKERESULT(0xFD, 2); // Memory allocation failure
            }
            LOG_HEAP(COMP_LDN_ICOM, "after SystemEvent");
        }

        // Create RyuLDN client
        if (this->ryuldn_client == nullptr) {
            LOG_INFO(COMP_LDN_ICOM, "Creating RyuLDN LdnMasterProxyClient");
            LOG_INFO_ARGS(COMP_LDN_ICOM, "sizeof(LdnMasterProxyClient): %zu bytes", sizeof(ryuldn::LdnMasterProxyClient));
            LOG_HEAP(COMP_LDN_ICOM, "before LdnMasterProxyClient");
            this->ryuldn_client = new (std::nothrow) ryuldn::LdnMasterProxyClient(g_RyuLdnConfig.address, g_RyuLdnConfig.port, false);

            // Check if allocation succeeded
            if (this->ryuldn_client == nullptr) {
                LOG_INFO(COMP_LDN_ICOM, "ERROR: Failed to allocate LdnMasterProxyClient - out of memory");
                return MAKERESULT(0xFD, 2); // Memory allocation failure
            }
            LOG_HEAP(COMP_LDN_ICOM, "after LdnMasterProxyClient");

            // IMPORTANT: Even if new() succeeded, the constructor might have failed internally
            // Check if the object is in a valid state by verifying Initialize() succeeds
            // (Initialize will fail if internal allocations failed)
            // Set network change callback
            this->ryuldn_client->SetNetworkChangeCallback([this](const NetworkInfo& info, bool connected) {
                this->onNetworkChange(info, connected);
            });

            // Set proxy config callback
            this->ryuldn_client->SetProxyConfigCallback([this]([[maybe_unused]] const ryuldn::LdnHeader& header, const ryuldn::ProxyConfig& config) {
                // Create and register proxy when we receive ProxyConfig
                if (this->ryuldn_proxy == nullptr && config.proxyIp != 0) {
                    // Create proxy with protocol access
                    LOG_INFO_ARGS(COMP_LDN_ICOM, "sizeof(LdnProxy): %zu bytes", sizeof(ryuldn::proxy::LdnProxy));
                    LOG_HEAP(COMP_LDN_ICOM, "before LdnProxy");
                    this->ryuldn_proxy = new (std::nothrow) ryuldn::proxy::LdnProxy(config, this->ryuldn_client, this->ryuldn_client->GetProtocol());

                    if (this->ryuldn_proxy == nullptr) {
                        LOG_INFO(COMP_LDN_ICOM, "ERROR: Failed to allocate LdnProxy - out of memory");
                        return;
                    }
                    LOG_HEAP(COMP_LDN_ICOM, "after LdnProxy");
                    BsdMitmService::RegisterProxy(this->ryuldn_proxy);
                    LOG_INFO(COMP_LDN_ICOM, "RyuLDN proxy created and registered");
                }
            });

            // Note: Proxy data callback is now handled by LdnProxy via protocol registration
            // No need to manually forward anymore

            Result rc = this->ryuldn_client->Initialize();
            if (R_FAILED(rc)) {
                LOG_INFO_ARGS(COMP_LDN_ICOM, "Failed to initialize RyuLDN client: 0x%x", rc);
                delete this->ryuldn_client;
                this->ryuldn_client = nullptr;
                return rc;
            }
        }

        setState(CommState::Initialized);

        return ResultSuccess();
    }

    Result ICommunicationService::InitializeSystem2(u64 unk, const sf::ClientProcessId &client_process_id) {
        LOG_INFO_ARGS(COMP_LDN_ICOM, "ICommunicationService::InitializeSystem2 unk: %" PRIu64, unk);
        this->error_state = unk;
        return this->Initialize(client_process_id);
    }

    Result ICommunicationService::Finalize() {
        LOG_INFO(COMP_LDN_ICOM, "Finalize");

        // Destroy proxy first
        if (this->ryuldn_proxy) {
            BsdMitmService::UnregisterProxy();
            delete this->ryuldn_proxy;
            this->ryuldn_proxy = nullptr;
        }

        if (this->ryuldn_client) {
            Result rc = this->ryuldn_client->Finalize();
            delete this->ryuldn_client;
            this->ryuldn_client = nullptr;

            if (R_FAILED(rc)) {
                return rc;
            }
        }

        if (this->state_event) {
            delete this->state_event;
            this->state_event = nullptr;
        }

        setState(CommState::None);

        return ResultSuccess();
    }

    Result ICommunicationService::OpenAccessPoint() {
        LOG_INFO(COMP_LDN_ICOM, "OpenAccessPoint");

        if (current_state != CommState::Initialized) {
            return MAKERESULT(0xCB, 32);
        }

        setState(CommState::AccessPoint);
        return ResultSuccess();
    }

    Result ICommunicationService::CloseAccessPoint() {
        LOG_INFO(COMP_LDN_ICOM, "CloseAccessPoint");

        if (current_state == CommState::AccessPointCreated) {
            if (ryuldn_client) {
                (void)ryuldn_client->DisconnectNetwork();
            }
        }

        if (current_state == CommState::AccessPoint || current_state == CommState::AccessPointCreated) {
            setState(CommState::Initialized);
            return ResultSuccess();
        }

        return MAKERESULT(0xCB, 32);
    }

    Result ICommunicationService::DestroyNetwork() {
        LOG_INFO(COMP_LDN_ICOM, "DestroyNetwork");

        if (current_state != CommState::AccessPointCreated) {
            return MAKERESULT(0xCB, 32);
        }

        if (ryuldn_client) {
            (void)ryuldn_client->DisconnectNetwork();
        }

        setState(CommState::AccessPoint);
        return ResultSuccess();
    }

    Result ICommunicationService::OpenStation() {
        LOG_INFO(COMP_LDN_ICOM, "OpenStation");

        if (current_state != CommState::Initialized) {
            return MAKERESULT(0xCB, 32);
        }

        setState(CommState::Station);
        return ResultSuccess();
    }

    Result ICommunicationService::CloseStation() {
        LOG_INFO(COMP_LDN_ICOM, "CloseStation");

        if (current_state == CommState::StationConnected) {
            if (ryuldn_client) {
                (void)ryuldn_client->DisconnectNetwork();
            }
        }

        if (current_state == CommState::Station || current_state == CommState::StationConnected) {
            setState(CommState::Initialized);
            return ResultSuccess();
        }

        return MAKERESULT(0xCB, 32);
    }

    Result ICommunicationService::Disconnect() {
        LOG_INFO(COMP_LDN_ICOM, "Disconnect");

        if (current_state != CommState::StationConnected) {
            return MAKERESULT(0xCB, 32);
        }

        if (ryuldn_client) {
            (void)ryuldn_client->DisconnectNetwork();
        }

        setState(CommState::Station);
        return ResultSuccess();
    }

    Result ICommunicationService::CreateNetwork(CreateNetworkConfig data) {
        LOG_INFO(COMP_LDN_ICOM, "CreateNetwork");

        if (current_state != CommState::AccessPoint) {
            return MAKERESULT(0xCB, 32);
        }

        if (!ryuldn_client) {
            return MAKERESULT(0xFD, 1);
        }

        // Build CreateAccessPointRequest
        ryuldn::CreateAccessPointRequest request;
        request.securityConfig = data.securityConfig;
        request.userConfig     = data.userConfig;
        request.networkConfig  = data.networkConfig;
        std::memset(&request.ryuNetworkConfig, 0, sizeof(request.ryuNetworkConfig));

        u8  advertiseData[AdvertiseDataSizeMax] = {0};
        u16 advertiseDataSize = 0;

        Result rc = ryuldn_client->CreateNetwork(request, advertiseData, advertiseDataSize);
        if (R_FAILED(rc)) {
            LOG_INFO_ARGS(COMP_LDN_ICOM, "ICommunicationService::CreateNetwork: RyuLDN CreateNetwork failed: 0x%x", rc);
            return rc;
        }

        LOG_INFO(COMP_LDN_ICOM, "ICommunicationService::CreateNetwork: RyuLDN CreateNetwork succeeded");
        return ResultSuccess();
    }

    Result ICommunicationService::SetAdvertiseData(sf::InAutoSelectBuffer data) {
        LOG_INFO_ARGS(COMP_LDN_ICOM, "SetAdvertiseData size: %zu", data.GetSize());

        if (current_state != CommState::AccessPoint && current_state != CommState::AccessPointCreated) {
            return MAKERESULT(0xCB, 32);
        }

        if (!ryuldn_client) {
            return MAKERESULT(0xFD, 1);
        }

        if (data.GetSize() > AdvertiseDataSizeMax) {
            return MAKERESULT(0xFD, 10);
        }

        return ryuldn_client->SetAdvertiseData(reinterpret_cast<const u8*>(data.GetPointer()), data.GetSize());
    }

    Result ICommunicationService::GetState(sf::Out<u32> state) {
        state.SetValue(static_cast<u32>(current_state));

        if (this->error_state) {
            if (disconnect_reason != ryuldn::DisconnectReason::None) {
                return MAKERESULT(0x10, static_cast<u32>(disconnect_reason));
            }
        }

        return ResultSuccess();
    }

    Result ICommunicationService::GetIpv4Address(sf::Out<u32> address, sf::Out<u32> netmask) {
        LOG_INFO_ARGS(COMP_LDN_ICOM, "GetIpv4Address: state=%d (0x%x)", static_cast<u32>(current_state), static_cast<u32>(current_state));

        // Accepte TOUS les états où on a un réseau actif
        if (current_state != CommState::AccessPointCreated && 
            current_state != CommState::StationConnected && 
            current_state != static_cast<CommState>(3)) {  // state 3 des logs
            LOG_INFO_ARGS(COMP_LDN_ICOM, "GetIpv4Address: invalid state %d -> ERROR 0xCB00320", static_cast<u32>(current_state));
            return MAKERESULT(0xCB, 32);
        }

        if (!ryuldn_client) {
            LOG_INFO(COMP_LDN_ICOM, "GetIpv4Address: no ryuldn_client");
            return MAKERESULT(0xFD, 1);
        }

        const ryuldn::ProxyConfig& config = ryuldn_client->GetProxyConfig();

        if (config.proxyIp == 0) {
            u32 gateway, primary_dns, secondary_dns;
            Result rc = nifmGetCurrentIpConfigInfo(address.GetPointer(), netmask.GetPointer(), &gateway, &primary_dns, &secondary_dns);
            if (R_SUCCEEDED(rc)) {
                address.SetValue(ntohl(address.GetValue()));
                netmask.SetValue(ntohl(netmask.GetValue()));
                LOG_INFO_ARGS(COMP_LDN_ICOM, "GetIpv4Address: nifm IP=0x%08x mask=0x%08x", address.GetValue(), netmask.GetValue());
            } else {
                LOG_INFO_ARGS(COMP_LDN_ICOM, "GetIpv4Address: nifm FAILED 0x%x", rc);
            }
            return rc;
        } else {
            LOG_INFO_ARGS(COMP_LDN_ICOM, "GetIpv4Address: proxy IP=0x%08x mask=0x%08x ✓", config.proxyIp, config.proxySubnetMask);
            address.SetValue(config.proxyIp);
            netmask.SetValue(config.proxySubnetMask);
        }

        return ResultSuccess();
    }


    Result ICommunicationService::GetNetworkInfo(sf::Out<NetworkInfo> info) {
        LOG_INFO_ARGS(COMP_LDN_ICOM, "GetNetworkInfo: state=%d (0x%x)", static_cast<u32>(current_state), static_cast<u32>(current_state));
        info.SetValue(network_info);
        return ResultSuccess();
    }

    Result ICommunicationService::GetDisconnectReason(sf::Out<u32> reason) {
        u32 local_reason = static_cast<u32>(disconnect_reason);
        LOG_INFO_ARGS(COMP_LDN_ICOM, "GetDisconnectReason: local=%u (%s)", local_reason, DisconnectReasonToString(local_reason));

        if (ryuldn_client) {
            auto r = ryuldn_client->GetDisconnectReason();
            u32 ryu_reason = static_cast<u32>(r);
            if (r != ryuldn::DisconnectReason::None) {
                LOG_INFO_ARGS(COMP_LDN_ICOM, "GetDisconnectReason: RyuLDN=%u (%s)", ryu_reason, DisconnectReasonToString(ryu_reason));
                reason.SetValue(ryu_reason);
                return ResultSuccess();
            }
        }

        LOG_INFO_ARGS(COMP_LDN_ICOM, "GetDisconnectReason: using local=%u (%s)", local_reason, DisconnectReasonToString(local_reason));
        reason.SetValue(local_reason);
        return ResultSuccess();
    }

    Result ICommunicationService::GetNetworkInfoLatestUpdate(sf::Out<NetworkInfo> buffer, sf::OutArray<NodeLatestUpdate> pUpdates) {
        LOG_INFO_ARGS(COMP_LDN_ICOM, "GetNetworkInfoLatestUpdate buffer %p pUpdates %p count %zu",
                  buffer.GetPointer(), pUpdates.GetPointer(), pUpdates.GetSize());

        if (current_state != CommState::AccessPointCreated && current_state != CommState::StationConnected) {
            return MAKERESULT(0xCB, 32);
        }

        buffer.SetValue(network_info);

        size_t count = std::min(pUpdates.GetSize(), static_cast<size_t>(NodeCountMax));
        for (size_t i = 0; i < count; i++) {
            pUpdates[i] = node_latest_updates[i];
            // Clear after consumption
            node_latest_updates[i].stateChange = NodeStateChange_None;
        }

        return ResultSuccess();
    }

    Result ICommunicationService::GetSecurityParameter(sf::Out<SecurityParameter> out) {
        LOG_INFO(COMP_LDN_ICOM, "GetSecurityParameter");

        if (current_state != CommState::AccessPointCreated && current_state != CommState::StationConnected) {
            return MAKERESULT(0xCB, 32);
        }

        SecurityParameter param;
        std::memset(&param, 0, sizeof(param));
        param.sessionId = network_info.networkId.sessionId;

        out.SetValue(param);
        return ResultSuccess();
    }

    Result ICommunicationService::GetNetworkConfig(sf::Out<NetworkConfig> out) {
        LOG_INFO(COMP_LDN_ICOM, "GetNetworkConfig");

        if (current_state != CommState::AccessPointCreated && current_state != CommState::StationConnected) {
            return MAKERESULT(0xCB, 32);
        }

        NetworkConfig config;
        config.intentId = network_info.networkId.intentId;
        config.channel = network_info.common.channel;
        config.nodeCountMax = network_info.ldn.nodeCountMax;
        config.localCommunicationVersion = network_info.ldn.nodes[0].localCommunicationVersion;
        std::memset(config._unk2, 0, sizeof(config._unk2));

        out.SetValue(config);
        return ResultSuccess();
    }

    Result ICommunicationService::AttachStateChangeEvent(sf::Out<sf::CopyHandle> handle) {
        handle.SetValue(this->state_event->GetReadableHandle(), false);
        return ResultSuccess();
    }

    Result ICommunicationService::Scan(sf::Out<u32> outCount, sf::OutAutoSelectArray<NetworkInfo> buffer, u16 channel, ScanFilter filter) {
        AMS_UNUSED(channel);
        LOG_INFO(COMP_LDN_ICOM, "Scan");

        if (current_state < CommState::Initialized) {
            return MAKERESULT(0xCB, 32);
        }

        if (!ryuldn_client) {
            outCount.SetValue(0);
            return ResultSuccess();
        }

        u16 count = buffer.GetSize();
        Result rc = ryuldn_client->Scan(buffer.GetPointer(), &count, filter);

        outCount.SetValue(count);

        LOG_INFO_ARGS(COMP_LDN_ICOM, "Scan found %d networks, rc=%d", count, rc);

        return rc;
    }

    Result ICommunicationService::Connect(ConnectNetworkData param, const NetworkInfo &data) {
        LOG_INFO(COMP_LDN_ICOM, "Connect");

        if (current_state != CommState::Station) {
            return MAKERESULT(0xCB, 32);
        }

        if (!ryuldn_client) {
            return MAKERESULT(0xFD, 1);
        }

        ryuldn::ConnectRequest request;
        request.securityConfig = param.securityConfig;
        request.userConfig = param.userConfig;
        request.localCommunicationVersion = param.localCommunicationVersion;
        request.optionUnknown = param.option;
        request.networkInfo = data;

        Result rc = ryuldn_client->Connect(request);
        if (R_FAILED(rc)) {
            LOG_INFO_ARGS(COMP_LDN_ICOM, "Failed to connect: 0x%x", rc);
            return rc;
        }

        return ResultSuccess();
    }

    // NYI functions
    Result ICommunicationService::SetStationAcceptPolicy(u8 policy) {
        LOG_INFO_ARGS(COMP_LDN_ICOM, "SetStationAcceptPolicy: %d", policy);

        if (current_state != CommState::AccessPoint && current_state != CommState::AccessPointCreated) {
            return MAKERESULT(0xCB, 32);
        }

        if (ryuldn_client) {
            return ryuldn_client->SetStationAcceptPolicy(policy);
        }

        return ResultSuccess();
    }

    Result ICommunicationService::SetWirelessControllerRestriction() {
        return ResultSuccess();
    }

    Result ICommunicationService::ScanPrivate() {
        return ResultSuccess();
    }

    Result ICommunicationService::CreateNetworkPrivate() {
        return ResultSuccess();
    }

    Result ICommunicationService::Reject() {
        return ResultSuccess();
    }

    Result ICommunicationService::AddAcceptFilterEntry() {
        return ResultSuccess();
    }

    Result ICommunicationService::ClearAcceptFilter() {
        return ResultSuccess();
    }

    Result ICommunicationService::ConnectPrivate() {
        return ResultSuccess();
    }
}
