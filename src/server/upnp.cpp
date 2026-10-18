#include "upnp.h"

#include <httplib.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define GK_CLOSE closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
#define GK_CLOSE close
#define INVALID_SOCKET (-1)
#endif

namespace gk {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The text between <tag> and </tag> after position `from`, or "".
std::string xml_text(const std::string& xml, const std::string& tag, size_t from = 0, size_t* end = nullptr) {
    const std::string open = "<" + tag + ">", close = "</" + tag + ">";
    const size_t a = xml.find(open, from);
    if (a == std::string::npos) return {};
    const size_t b = xml.find(close, a);
    if (b == std::string::npos) return {};
    if (end) *end = b + close.size();
    return xml.substr(a + open.size(), b - a - open.size());
}

struct Url {
    std::string scheme_host; // http://host:port
    std::string path;
};
Url split_url(const std::string& url) {
    const size_t p = url.find("://");
    const size_t s = url.find('/', p == std::string::npos ? 0 : p + 3);
    if (s == std::string::npos) return {url, "/"};
    return {url.substr(0, s), url.substr(s)};
}

// Sends an SSDP search for gateways and returns the description URLs heard.
std::vector<std::string> discover(int timeout_ms) {
    std::vector<std::string> out;
    socket_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return out;
#if defined(_WIN32)
    DWORD tv = 300;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof tv);
#else
    timeval tv{0, 300 * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
    for (const char* st : {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                           "urn:schemas-upnp-org:service:WANIPConnection:1"}) {
        const std::string msg = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") + st + "\r\n\r\n";
        sendto(s, msg.data(), int(msg.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char buf[2048];
    while (std::chrono::steady_clock::now() < deadline) {
        const int n = int(recv(s, buf, sizeof buf - 1, 0));
        if (n <= 0) {
            if (!out.empty()) break; // heard something and then silence: done
            continue;
        }
        buf[n] = 0;
        const std::string reply(buf, size_t(n)), low = lower(reply);
        const size_t at = low.find("\nlocation:");
        if (at == std::string::npos) continue;
        size_t a = at + 10;
        while (a < reply.size() && reply[a] == ' ') ++a;
        const size_t b = reply.find_first_of("\r\n", a);
        const std::string loc = reply.substr(a, b == std::string::npos ? std::string::npos : b - a);
        bool seen = false;
        for (const auto& o : out) seen |= o == loc;
        if (!seen) out.push_back(loc);
    }
    GK_CLOSE(s);
    return out;
}

std::string soap(const std::string& control_url, const std::string& service, const std::string& action, const std::string& args,
                 int* status) {
    const Url u = split_url(control_url);
    httplib::Client cli(u.scheme_host);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(3);
    const std::string body = "<?xml version=\"1.0\"?>\r\n"
                             "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>"
                             "<u:" + action + " xmlns:u=\"" + service + "\">" + args + "</u:" + action + "></s:Body></s:Envelope>";
    httplib::Headers h{{"SOAPAction", "\"" + service + "#" + action + "\""}};
    auto res = cli.Post(u.path, h, body, "text/xml; charset=\"utf-8\"");
    if (status) *status = res ? res->status : 0;
    return res ? res->body : std::string();
}

} // namespace

std::string internet_ip() {
    httplib::Client cli("http://api.ipify.org");
    cli.set_connection_timeout(3);
    cli.set_read_timeout(3);
    auto res = cli.Get("/");
    if (!res || res->status != 200 || res->body.size() > 45) return {};
    return res->body;
}

bool is_private_ip(const std::string& ip) {
    int a = 0, b = 0;
    if (std::sscanf(ip.c_str(), "%d.%d", &a, &b) != 2) return false;
    return a == 10 || a == 127 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127);
}

std::string local_ip() {
    // Connecting a UDP socket sends nothing but picks the outgoing interface.
    socket_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return {};
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
    std::string ip;
    if (connect(s, reinterpret_cast<sockaddr*>(&to), sizeof to) == 0) {
        sockaddr_in me{};
        socklen_t len = sizeof me;
        if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0) {
            char buf[64] = {};
            inet_ntop(AF_INET, &me.sin_addr, buf, sizeof buf);
            ip = buf;
        }
    }
    GK_CLOSE(s);
    return ip == "0.0.0.0" ? std::string() : ip;
}

PortForward Upnp::forward_udp(uint16_t port, const std::string& description) {
    PortForward r;
    r.local_ip = local_ip();
    if (r.local_ip.empty()) {
        r.error = "no network";
        return r;
    }
    const auto gateways = discover(2200);
    if (gateways.empty()) {
        r.error = "no UPnP router answered (it may be switched off in the router's settings)";
        return r;
    }
    for (const std::string& loc : gateways) {
        const Url u = split_url(loc);
        httplib::Client cli(u.scheme_host);
        cli.set_connection_timeout(2);
        cli.set_read_timeout(3);
        auto res = cli.Get(u.path);
        if (!res || res->status != 200) continue;
        const std::string& xml = res->body;
        // The WAN connection service and where to send it commands.
        size_t pos = 0;
        while (true) {
            size_t end = 0;
            const std::string service = xml_text(xml, "service", pos, &end);
            if (service.empty()) break;
            pos = end;
            const std::string type = xml_text(service, "serviceType");
            if (type.find("WANIPConnection") == std::string::npos && type.find("WANPPPConnection") == std::string::npos) continue;
            std::string control = xml_text(service, "controlURL");
            if (control.empty()) continue;
            if (control.rfind("http", 0) != 0) {
                std::string base = xml_text(xml, "URLBase");
                if (base.empty()) base = u.scheme_host;
                while (!base.empty() && base.back() == '/') base.pop_back();
                control = base + (control.front() == '/' ? "" : "/") + control;
            }
            const std::string p = std::to_string(port);
            int status = 0;
            for (const char* lease : {"86400", "0"}) { // a day, so a crashed server's mapping lapses; some routers only take 0
                soap(control, type, "AddPortMapping",
                     "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + p + "</NewExternalPort><NewProtocol>UDP</NewProtocol>"
                     "<NewInternalPort>" + p + "</NewInternalPort><NewInternalClient>" + r.local_ip + "</NewInternalClient>"
                     "<NewEnabled>1</NewEnabled><NewPortMappingDescription>" + description + "</NewPortMappingDescription>"
                     "<NewLeaseDuration>" + lease + "</NewLeaseDuration>",
                     &status);
                if (status == 200) break;
            }
            if (status != 200) {
                r.error = "the router refused the port mapping (HTTP " + std::to_string(status) + ")";
                continue;
            }
            control_url_ = control;
            service_type_ = type;
            mapped_port_ = port;
            r.ok = true;
            r.error.clear();
            r.public_ip = xml_text(soap(control, type, "GetExternalIPAddress", "", &status), "NewExternalIPAddress");
            return r;
        }
    }
    if (r.error.empty()) r.error = "the router has no WAN connection service";
    return r;
}

void Upnp::remove() {
    if (!mapped_port_) return;
    int status = 0;
    soap(control_url_, service_type_, "DeletePortMapping",
         "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(mapped_port_) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>",
         &status);
    mapped_port_ = 0;
}

} // namespace gk
