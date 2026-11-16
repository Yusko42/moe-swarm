#pragma once  // NOLINT(llvm-header-guard)

#include <string>

#include "registry/node_registry.hpp"

namespace httplib {
class Server;
}

namespace swarm::initiator {

class HttpServer {
   public:
    explicit HttpServer(NodeRegistry& registry, int port);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Пытается занять порт. Если port == 0 — ОС выберет свободный (для тестов).
    // Возвращает false, если порт занят или произошла ошибка bind.
    bool bind();

    [[nodiscard]] std::string address() const;

    [[nodiscard]] int port() const { return port_; }

    void run();

    void stop();

   private:
    void setup_routes();

    NodeRegistry& registry_;
    int port_;            // запрошенный порт (может быть 0)
    int bound_port_ = 0;  // фактический после bind()
    std::string address_;
    httplib::Server* srv_;
};

}  // namespace swarm::initiator