/*
 * Copyright (c) 2018 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#include <switch.h>
#include "debug.hpp"

#include "ryuldnnx_service.hpp"
#include "ldn_icommunication.hpp"
#include "ldn_client_process_monitor.hpp"

namespace ams::mitm::ldn {

    using ObjectFactory = ams::sf::ObjectFactory<ams::sf::StdAllocationPolicy<std::allocator>>;

    RyuLdnNXService::RyuLdnNXService(std::shared_ptr<::Service> &&s, const sm::MitmProcessInfo &c) : sf::MitmServiceImplBase(std::forward<std::shared_ptr<::Service>>(s), c)
    {
        LogFormat("RyuLdnNXService created");
    }

    Result RyuLdnNXService::CreateUserLocalCommunicationService(sf::Out<sf::SharedPointer<ICommunicationInterface>> out) {
        LogFormat("CreateUserLocalCommunicationService: enabled %d", static_cast<u32>(LdnConfig::IsEnabled()));

        if (LdnConfig::IsEnabled()) {
            out.SetValue(sf::CreateSharedObjectEmplaced<ICommunicationInterface, ICommunicationService>());
            return 0;
        }

        return sm::mitm::ResultShouldForwardToSession();
    }

    Result RyuLdnNXService::CreateClientProcessMonitor(sf::Out<sf::SharedPointer<IClientProcessMonitorInterface>> out) {
        LogFormat("CreateClientProcessMonitor called");

        // Always create the monitor, regardless of ryuldnnx being enabled
        // This is required for firmware 18.0.0+ compatibility (Pokemon Legends Z-A)
        out.SetValue(sf::CreateSharedObjectEmplaced<IClientProcessMonitorInterface, IClientProcessMonitor>());
        return 0;
    }

    Result RyuLdnNXService::CreateRyuLdnNXConfigService(sf::Out<sf::SharedPointer<ILdnConfig>> out) {
        out.SetValue(sf::CreateSharedObjectEmplaced<ILdnConfig, LdnConfig>());
        return 0;
    }
}