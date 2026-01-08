# Add netinet/tcp.h include after arpa/inet.h
6a\
#include <netinet/tcp.h>
# Fix socket call with ::socket
s/_socket = socket(/_socket = ::socket(/
# Fix SystemEvent calls - add .GetBase()
s/os::SignalSystemEvent(&_connectedEvent)/os::SignalSystemEvent(\&_connectedEvent.GetBase())/g
s/os::SignalSystemEvent(&_readyEvent)/os::SignalSystemEvent(\&_readyEvent.GetBase())/g
s/os::ClearSystemEvent(&_connectedEvent)/os::ClearSystemEvent(\&_connectedEvent.GetBase())/g
s/os::ClearSystemEvent(&_readyEvent)/os::ClearSystemEvent(\&_readyEvent.GetBase())/g
s/os::TimedWaitSystemEvent(&_connectedEvent,/os::TimedWaitSystemEvent(\&_connectedEvent.GetBase(),/g
s/os::TimedWaitSystemEvent(&_readyEvent,/os::TimedWaitSystemEvent(\&_readyEvent.GetBase(),/g
# Fix unused parameter
s/void P2pProxyClient::HandleProxyConfig(const LdnHeader& header,/void P2pProxyClient::HandleProxyConfig([[maybe_unused]] const LdnHeader\& header,/
