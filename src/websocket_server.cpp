#include "websocket_server.hpp"

#include <chrono>
#include <nlohmann/json.hpp>

WebSocketServer::WebSocketServer(RosbridgeProtocol& protocol)
    : protocol_(protocol)
{
}

WebSocketServer::~WebSocketServer()
{
    stop();
}

bool WebSocketServer::start(const std::string& host, uint16_t port)
{
    server_ = std::make_unique<ix::WebSocketServer>(port, host);
    server_->setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState> connection,
            ix::WebSocket& socket, const ix::WebSocketMessagePtr& message)
    {
        on_message(std::move(connection), socket, message);
    });
    return server_->listenAndStart();
}

void WebSocketServer::stop()
{
    if (server_) server_->stop();
}

void WebSocketServer::on_message(std::shared_ptr<ix::ConnectionState> connection,
        ix::WebSocket& socket, const ix::WebSocketMessagePtr& message)
{
    if (!connection || !message)
    {
        return;
    }
    const auto id = connection->getId();
    if (message->type == ix::WebSocketMessageType::Open)
    {
        std::shared_ptr<ix::WebSocket> client;
        for (const auto& connected : server_->getClients())
        {
            if (connected.get() == &socket)
            {
                client = connected;
                break;
            }
        }
        std::lock_guard<std::mutex> lock(clients_mutex_);
        if (client) clients_[id] = std::move(client);
        return;
    }
    if (message->type == ix::WebSocketMessageType::Close || message->type == ix::WebSocketMessageType::Error)
    {
        protocol_.disconnect(id);
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(id);
        return;
    }
    if (message->type != ix::WebSocketMessageType::Message || message->binary)
    {
        return;
    }

    try
    {
        const auto request = nlohmann::json::parse(message->str);
        for (const auto& response : protocol_.handle(id, request))
        {
            socket.sendText(response.dump());
        }
    }
    catch (const std::exception& error)
    {
        socket.sendText(nlohmann::json{
            {"op", "status"}, {"level", "error"}, {"msg", std::string("invalid JSON: ") + error.what()}
        }.dump());
    }
}

void WebSocketServer::publish(const PublishedSample& sample)
{
    for (const auto& client_id : protocol_.subscribers(sample.topic))
    {
        std::shared_ptr<ix::WebSocket> client;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            const auto found = clients_.find(client_id);
            if (found != clients_.end()) client = found->second;
        }
        if (client)
        {
            if (protocol_.compression(client_id, sample.topic) == "cbor-raw" && !sample.serialized_ros2.empty())
            {
                const auto now = std::chrono::system_clock::now().time_since_epoch();
                const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
                const auto seconds = nanoseconds / 1000000000;
                nlohmann::json envelope = {{"op", "publish"}, {"topic", sample.topic},
                    {"type", sample.wire_type}, {"msg", {{"bytes", nlohmann::json::binary(sample.serialized_ros2)},
                        {"secs", seconds}, {"nsecs", nanoseconds % 1000000000}}}};
                const auto cbor = nlohmann::json::to_cbor(envelope);
                client->sendBinary(std::string(cbor.begin(), cbor.end()));
            }
            else
            {
                const auto envelope = nlohmann::json{
                    {"op", "publish"}, {"topic", sample.topic}, {"type", sample.wire_type}, {"msg", sample.message}
                }.dump();
                client->sendText(envelope);
            }
        }
    }
}
