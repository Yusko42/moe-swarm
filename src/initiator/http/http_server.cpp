#include "http_server.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#include <httplib.h>
#include <nlohmann/json.hpp>
#pragma GCC diagnostic pop

namespace swarm::initiator {
namespace {

bool is_virtual_interface(std::string_view name) {
    static constexpr std::string_view kPrefixes[] = {"docker", "br-",   "veth", "virbr",
                                                     "tun",    "tap",   "amn",  "tailscale"};
    for (const auto p : kPrefixes) {
        if (name.starts_with(p)) {
            return true;
        }
    }
    return false;
}

std::string detect_lan_ip() {
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) {
        return "127.0.0.1";
    }

    std::string result;
    for (const ifaddrs* it = list; it != nullptr; it = it->ifa_next) {
        if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((it->ifa_flags & IFF_LOOPBACK) != 0 || (it->ifa_flags & IFF_UP) == 0) {
            continue;
        }
        if ((it->ifa_flags & IFF_POINTOPOINT) != 0) {
            continue;  // VPN /32
        }
        if (is_virtual_interface(it->ifa_name != nullptr ? it->ifa_name : "")) {
            continue;
        }

        char buf[INET_ADDRSTRLEN] = {};
        const auto* sin = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
        if (::inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)) != nullptr) {
            result = buf;
            break;
        }
    }
    ::freeifaddrs(list);
    return result.empty() ? "127.0.0.1" : result;
}

}  // namespace

HttpServer::HttpServer(NodeRegistry& registry, int port)
    : registry_(registry), port_(port), srv_(new httplib::Server()) {}

HttpServer::~HttpServer() {
    delete srv_;
}

bool HttpServer::bind() {
    // httplib по умолчанию ставит SO_REUSEPORT|SO_REUSEADDR, если ОС позволяет
    // Оставляем только ReuseAddr: bind на активный LISTEN теперь вернёт ошибку
    // Проверка на основе кода из README httplib 
    srv_->set_socket_options([](socket_t sock) {
#ifdef _WIN32
    httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
    httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
});

    if (port_ == 0) {
        const int p = srv_->bind_to_any_port("0.0.0.0");
        if (p <= 0) {
            return false;
        }
        bound_port_ = p;
    } else {
        if (!srv_->bind_to_port("0.0.0.0", port_)) {
            return false;
        }
        bound_port_ = port_;
    }
    address_ = detect_lan_ip() + ":" + std::to_string(bound_port_);
    setup_routes();
    return true;
}

std::string HttpServer::address() const { return address_; }

void HttpServer::run() { (void)srv_->listen_after_bind(); }

void HttpServer::stop() { srv_->stop(); }

void HttpServer::setup_routes() {
    using nlohmann::json;

    srv_->Post("/agent/v1/join", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            res.status = 400;
            res.set_content(R"({"error":{"code":"bad_json"}})", "application/json");
            return;
        }

        const std::string agent_instance_id = body.value("agent_instance_id", "");
        const std::string name              = body.value("name", "");
        const std::string rpc_host          = body.value("rpc_host", "");
        const std::uint64_t memory_limit    = body.value("memory_limit_bytes", 0ULL);

        if (agent_instance_id.empty() || name.empty()) {
            res.status = 400;
            res.set_content(R"({"error":{"code":"missing_fields"}})", "application/json");
            return;
        }

        const auto rec = registry_.join(agent_instance_id, name, memory_limit, rpc_host);

        const auto gib = rec.memory_limit_bytes / (1024ULL * 1024ULL * 1024ULL);
        std::cout << "Подключился узел " << rec.name << ": " << gib << " ГБ" << std::endl;

        json reply{{"swarm_id", registry_.swarm_id()},
                   {"node_id", rec.node_id},
                   {"heartbeat_interval_ms", 1000},
                   {"node_timeout_ms", 3000},
                   {"model_state", "idle"}};
        res.set_content(reply.dump(), "application/json");
    });

    srv_->Post("/agent/v1/heartbeat", [this](const httplib::Request& req, httplib::Response& res) {
        const auto body = json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            res.status = 400;
            res.set_content(R"({"error":{"code":"bad_json"}})", "application/json");
            return;
        }

        const std::string node_id = body.value("node_id", "");
        if (node_id.empty()) {
            res.status = 400;
            res.set_content(R"({"error":{"code":"missing_node_id"}})", "application/json");
            return;
        }

        const auto existing = registry_.find(node_id);
        if (!existing.has_value()) {
            res.status = 404;
            res.set_content(R"({"error":{"code":"unknown_node"}})", "application/json");
            return;
        }

        std::string state;
        std::string deployment_id;
        int         port = 0;
        if (body.contains("rpc") && body["rpc"].is_object()) {
            state         = body["rpc"].value("state", "stopped");
            deployment_id = body["rpc"].value("deployment_id", "");
            port          = body["rpc"].value("port", 0);
        }

        const bool meaningful = registry_.update_rpc_state(node_id, state, deployment_id, port);
        if (meaningful) {
            const int effective_port = (port != 0) ? port : existing->rpc_port;
            std::cout << "rpc-server работает: " << existing->rpc_host << ":" << effective_port << std::endl;
        }

        json reply{{"node_state", "connected"}, {"model_state", "idle"}, {"commands", json::array()}};
        res.set_content(reply.dump(), "application/json");
    });
}

}  // namespace swarm::initiator