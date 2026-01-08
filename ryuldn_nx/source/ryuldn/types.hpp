#pragma once
// RyuLDN Types - Complete Include
// This header includes all RyuLDN protocol types
// Organized to match Ryujinx LdnRyu/Types/ structure

// Core protocol types
#include "types/packet_id.hpp"
#include "types/ldn_header.hpp"
#include "types/network_error.hpp"
#include "types/disconnect_reason.hpp"

// Protocol message types
#include "types/initialize_message.hpp"
#include "types/passphrase_message.hpp"
#include "types/ping_message.hpp"
#include "types/disconnect_message.hpp"
#include "types/network_error_message.hpp"
#include "types/reject_request.hpp"
#include "types/set_accept_policy_request.hpp"

// Proxy types
#include "types/proxy_config.hpp"
#include "types/proxy_info.hpp"
#include "types/proxy_data_header.hpp"
#include "types/proxy_connect_request.hpp"
#include "types/proxy_connect_response.hpp"
#include "types/proxy_disconnect_message.hpp"

// External proxy types
#include "types/external_proxy_config.hpp"
#include "types/external_proxy_token.hpp"
#include "types/external_proxy_state.hpp"

// Network configuration types
#include "types/ryu_network_config.hpp"
#include "types/create_access_point_request.hpp"
#include "types/connect_request.hpp"

// LDN base types (from parent namespace)
#include "../ldn_types.hpp"
