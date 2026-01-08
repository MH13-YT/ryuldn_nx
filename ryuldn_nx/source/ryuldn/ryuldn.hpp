#pragma once
// RyuLDN - Main Include
// This is the main entry point for using RyuLDN functionality
// Structure matches Ryujinx LdnRyu implementation

// Core protocol and client
#include "ryu_ldn_protocol.hpp"
#include "ldn_master_proxy_client.hpp"
#include "network_timeout.hpp"

// Proxy functionality
#include "proxy/ldn_proxy.hpp"
#include "proxy/ldn_proxy_socket.hpp"
#include "proxy/proxy_helpers.hpp"
#include "proxy/ephemeral_port_pool.hpp"

// P2P Proxy functionality
#include "proxy/upnp_client.hpp"
#include "proxy/p2p_proxy_session.hpp"
#include "proxy/p2p_proxy_server.hpp"
#include "proxy/p2p_proxy_client.hpp"

// All type definitions (automatically includes all types/* files)
#include "types.hpp"
