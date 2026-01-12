#pragma once
#include <switch.h>
#include <stratosphere.hpp>
#include <cstring>
#include <atomic>
#include "ldn_icommunication.hpp"
#include "ldn_client_process_monitor.hpp"
#include "ryuldnnx_config.hpp"
#include "debug.hpp"
#include "interfaces/iservice.hpp"

namespace ams::mitm::ldn {

    class RyuLdnNXService : public sf::MitmServiceImplBase {
        private:
            using RedirectOnlyLocationResolverFactory = sf::ObjectFactory<sf::StdAllocationPolicy<std::allocator>>;
        public:
            RyuLdnNXService(std::shared_ptr<::Service> &&s, const sm::MitmProcessInfo &c);
            
            static bool ShouldMitm(const sm::MitmProcessInfo &client_info) {
                LogFormat("should_mitm pid: %" PRIu64 " tid: %" PRIx64, client_info.process_id, client_info.program_id);
                return true;
            }
        // protected:
        public:
            /* Overridden commands. */
            Result CreateUserLocalCommunicationService(sf::Out<sf::SharedPointer<ICommunicationInterface>> out);
            Result CreateClientProcessMonitor(sf::Out<sf::SharedPointer<IClientProcessMonitorInterface>> out);
            Result CreateRyuLdnNXConfigService(sf::Out<sf::SharedPointer<ILdnConfig>> out);
    };
    static_assert(ams::mitm::ldn::IsIRyuLdnNXService<RyuLdnNXService>);

}