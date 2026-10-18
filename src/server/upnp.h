#pragma once
// Automatic port forwarding with UPnP: asks the home router (an Internet
// Gateway Device) to forward a UDP port to this computer, so friends on
// the internet can join without anyone opening router settings.
#include <cstdint>
#include <string>

namespace gk {

struct PortForward {
    bool ok = false;
    std::string local_ip;    // this computer on the home network
    std::string public_ip;   // the router's internet address ("" if unknown)
    std::string error;       // why it didn't work, for the log
};

class Upnp {
public:
    // Finds the router (about 2 s at most) and forwards `port` (UDP).
    PortForward forward_udp(uint16_t port, const std::string& description);
    // Removes the mapping made by forward_udp, if any.
    void remove();
    ~Upnp() { remove(); }

private:
    std::string control_url_, service_type_;
    uint16_t mapped_port_ = 0;
};

// The internet's view of this connection (asks api.ipify.org over HTTP), or "".
std::string internet_ip();
// True for 10.x, 172.16-31.x, 192.168.x, 100.64-127.x (carrier NAT) and 127.x.
bool is_private_ip(const std::string& ip);

// This computer's address on the home network (the interface that routes
// to the internet), or "" without a network.
std::string local_ip();

} // namespace gk
