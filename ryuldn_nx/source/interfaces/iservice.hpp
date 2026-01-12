#pragma once
#include <stratosphere.hpp>
#include <cstring>
#include <atomic>
#include "../ldn_icommunication.hpp"
#include "../ldn_client_process_monitor.hpp"
#include "../ryuldnnx_config.hpp"
#include "../debug.hpp"

#define AMS_IRYULDNNX_SERVICE(C, H)                                                                                                          								\
    AMS_SF_METHOD_INFO(C, H, 0, 	Result, CreateUserLocalCommunicationService, 	(ams::sf::Out<ams::sf::SharedPointer<ams::mitm::ldn::ICommunicationInterface>> out), 	(out))	\
    AMS_SF_METHOD_INFO(C, H, 1, 	Result, CreateClientProcessMonitor, 			(ams::sf::Out<ams::sf::SharedPointer<ams::mitm::ldn::IClientProcessMonitorInterface>> out), (out))	\
    AMS_SF_METHOD_INFO(C, H, 65000, Result, CreateRyuLdnNXConfigService, 			(ams::sf::Out<ams::sf::SharedPointer<ams::mitm::ldn::ILdnConfig>> out), 				(out))	\

AMS_SF_DEFINE_MITM_INTERFACE(ams::mitm::ldn, IRyuLdnNXService, AMS_IRYULDNNX_SERVICE, 0x1D8F875E)