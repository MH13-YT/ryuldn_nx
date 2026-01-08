#include "ldn_icommunication.hpp"
#include "bsd_mitm_service.hpp"
#include <arpa/inet.h>
#include <cstring>

namespace ams::mitm::ldn {
    static_assert(sizeof(NetworkInfo) == 0x480, "sizeof(NetworkInfo) should be 0x480");
    static_assert(sizeof(ConnectNetworkData) == 0x7C, "sizeof(ConnectNetworkData) should be 0x7C");
    static_assert(sizeof(ScanFilter) == 0x60, "sizeof(ScanFilter) should be 0x60");

    // TODO: Read from config file
    static const char* RYULDN_SERVER_ADDRESS = "ldn.ryujinx.org";
    static const int RYULDN_SERVER_PORT = 10000;

    void ICommunicationService::setState(CommState state) {
        current_state = state;
        onEventFired();
    }

    void ICommunicationService::onNetworkChange(const NetworkInfo& info, bool connected) {
        if (connected) {
            network_info = info;
            if (current_state == CommState::AccessPoint) {
                setState(CommState::AccessPointCreated);
            } else if (current_state == CommState::Station) {
                setState(CommState::StationConnected);
            }
        } else {
            if (current_state == CommState::AccessPointCreated) {
                setState(CommState::AccessPoint);
            } else if (current_state == CommState::StationConnected) {
                setState(CommState::Station);
            }
        }
    }

    void ICommunicationService::onEventFired() {
        if (this->state_event) {
            LogFormat("onEventFired signal_event");
            this->state_event->Signal();
        }
    }

    Result ICommunicationService::Initialize(const sf::ClientProcessId &client_process_id) {
        LogFormat("ICommunicationService::Initialize pid: %" PRIu64, client_process_id.GetValue());

        if (this->state_event == nullptr) {
            // ClearMode, inter_process
            LogFormat("StateEvent is null");
            this->state_event = new os::SystemEvent(::ams::os::EventClearMode_AutoClear, true);
        }

        // Create RyuLDN client
        if (this->ryuldn_client == nullptr) {
            LogFormat("Creating RyuLDN LdnMasterProxyClient");
            this->ryuldn_client = new ryuldn::LdnMasterProxyClient(RYULDN_SERVER_ADDRESS, RYULDN_SERVER_PORT, false);

            // Set network change callback
            this->ryuldn_client->SetNetworkChangeCallback([this](const NetworkInfo& info, bool connected) {
                this->onNetworkChange(info, connected);
            });

            // Set proxy config callback
            this->ryuldn_client->SetProxyConfigCallback([this]([[maybe_unused]] const ryuldn::LdnHeader& header, const ryuldn::ProxyConfig& config) {
                // Create and register proxy when we receive ProxyConfig
                if (this->ryuldn_proxy == nullptr && config.proxyIp != 0) {
                    // Create proxy with protocol access
                    this->ryuldn_proxy = new ryuldn::proxy::LdnProxy(config, this->ryuldn_client, this->ryuldn_client->GetProtocol());
                    BsdMitmService::RegisterProxy(this->ryuldn_proxy);
                    LogFormat("RyuLDN proxy created and registered");
                }
            });

            // Note: Proxy data callback is now handled by LdnProxy via protocol registration
            // No need to manually forward anymore

            Result rc = this->ryuldn_client->Initialize();
            if (R_FAILED(rc)) {
                LogFormat("Failed to initialize RyuLDN client: 0x%x", rc);
                delete this->ryuldn_client;
                this->ryuldn_client = nullptr;
                return rc;
            }
        }

        setState(CommState::Initialized);

        return ResultSuccess();
    }

    Result ICommunicationService::InitializeSystem2(u64 unk, const sf::ClientProcessId &client_process_id) {
        LogFormat("ICommunicationService::InitializeSystem2 unk: %" PRIu64, unk);
        this->error_state = unk;
        return this->Initialize(client_process_id);
    }

    Result ICommunicationService::Finalize() {
        LogFormat("Finalize");

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
        LogFormat("OpenAccessPoint");

        if (current_state != CommState::Initialized) {
            return MAKERESULT(0xCB, 32);
        }

        setState(CommState::AccessPoint);
        return ResultSuccess();
    }

    Result ICommunicationService::CloseAccessPoint() {
        LogFormat("CloseAccessPoint");

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
        LogFormat("DestroyNetwork");

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
        LogFormat("OpenStation");

        if (current_state != CommState::Initialized) {
            return MAKERESULT(0xCB, 32);
        }

        setState(CommState::Station);
        return ResultSuccess();
    }

    Result ICommunicationService::CloseStation() {
        LogFormat("CloseStation");

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
        LogFormat("Disconnect");

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
        LogFormat("CreateNetwork");

        if (current_state != CommState::AccessPoint) {
            return MAKERESULT(0xCB, 32);
        }

        if (!ryuldn_client) {
            return MAKERESULT(0xFD, 1);
        }

        // Build CreateAccessPointRequest
        ryuldn::CreateAccessPointRequest request;
        request.securityConfig = data.securityConfig;
        request.userConfig = data.userConfig;
        request.networkConfig = data.networkConfig;
        std::memset(&request.ryuNetworkConfig, 0, sizeof(request.ryuNetworkConfig));

        // TODO: Get advertise data (for now, empty)
        u8 advertiseData[AdvertiseDataSizeMax] = {0};
        u16 advertiseDataSize = 0;

        Result rc = ryuldn_client->CreateNetwork(request, advertiseData, advertiseDataSize);
        if (R_FAILED(rc)) {
            LogFormat("Failed to create network: 0x%x", rc);
            return rc;
        }

        return ResultSuccess();
    }

    Result ICommunicationService::SetAdvertiseData(sf::InAutoSelectBuffer data) {
        LogFormat("SetAdvertiseData size: %zu", data.GetSize());

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
        LogFormat("GetIpv4Address");

        if (current_state != CommState::AccessPointCreated && current_state != CommState::StationConnected) {
            return MAKERESULT(0xCB, 32);
        }

        if (!ryuldn_client) {
            return MAKERESULT(0xFD, 1);
        }

        const ryuldn::ProxyConfig& config = ryuldn_client->GetProxyConfig();

        if (config.proxyIp == 0) {
            // No proxy IP, use nifm
            u32 gateway, primary_dns, secondary_dns;
            Result rc = nifmGetCurrentIpConfigInfo(address.GetPointer(), netmask.GetPointer(), &gateway, &primary_dns, &secondary_dns);

            if (R_SUCCEEDED(rc)) {
                address.SetValue(ntohl(address.GetValue()));
                netmask.SetValue(ntohl(netmask.GetValue()));
            }

            return rc;
        } else {
            LogFormat("Using proxy IP: 0x%08x", config.proxyIp);
            address.SetValue(config.proxyIp);
            netmask.SetValue(config.proxySubnetMask);
        }

        return ResultSuccess();
    }

    Result ICommunicationService::GetNetworkInfo(sf::Out<NetworkInfo> buffer) {
        LogFormat("GetNetworkInfo state: %d", static_cast<u32>(current_state));

        if (current_state != CommState::AccessPointCreated && current_state != CommState::StationConnected) {
            return MAKERESULT(0xCB, 32);
        }

        buffer.SetValue(network_info);
        return ResultSuccess();
    }

    Result ICommunicationService::GetDisconnectReason(sf::Out<u32> reason) {
        LogFormat("GetDisconnectReason: %u", static_cast<u32>(disconnect_reason));
        reason.SetValue(static_cast<u32>(disconnect_reason));
        return ResultSuccess();
    }

    Result ICommunicationService::GetNetworkInfoLatestUpdate(sf::Out<NetworkInfo> buffer, sf::OutArray<NodeLatestUpdate> pUpdates) {
        LogFormat("GetNetworkInfoLatestUpdate buffer %p pUpdates %p count %zu",
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
        LogFormat("GetSecurityParameter");

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
        LogFormat("GetNetworkConfig");

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
        LogFormat("Scan");

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

        LogFormat("Scan found %d networks, rc=%d", count, rc);

        return rc;
    }

    Result ICommunicationService::Connect(ConnectNetworkData param, const NetworkInfo &data) {
        LogFormat("Connect");

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
            LogFormat("Failed to connect: 0x%x", rc);
            return rc;
        }

        return ResultSuccess();
    }

    // NYI functions
    Result ICommunicationService::SetStationAcceptPolicy(u8 policy) {
        LogFormat("SetStationAcceptPolicy: %d", policy);

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
