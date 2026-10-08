#pragma once

#ifdef logError
#undef logError
#endif
#ifdef logInfo
#undef logInfo
#endif
#include <ixwebsocket/IXWebSocketServer.h>
#include "rosbridge_protocol.hpp"
#include <map>
#include <memory>
#include <mutex>

class WebSocketServer
{
public:
    explicit WebSocketServer(RosbridgeProtocol& protocol);
    ~WebSocketServer();

    bool start(const std::string& host, uint16_t port);
    void stop();
    void publish(const PublishedSample& sample);

private:
    void on_message(std::shared_ptr<ix::ConnectionState> connection, ix::WebSocket& socket,
            const ix::WebSocketMessagePtr& message);

    RosbridgeProtocol& protocol_;
    std::unique_ptr<ix::WebSocketServer> server_;
    std::mutex clients_mutex_;
    std::map<std::string, std::shared_ptr<ix::WebSocket>> clients_;
};
