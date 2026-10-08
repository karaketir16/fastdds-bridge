#include "config.hpp"
#include "dds_topic.hpp"
#include "rosbridge_protocol.hpp"
#include "websocket_server.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <csignal>
#include <stdexcept>
#include <thread>

namespace
{
std::atomic<bool> running{true};
void stop(int)
{
    running = false;
}
}

int main(int argc, char** argv)
{
    std::filesystem::path config_path = "config/bridge.yaml";
    bool validate_only = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--config" && i + 1 < argc)
        {
            config_path = argv[++i];
        }
        else if (argument == "--validate-config")
        {
            validate_only = true;
        }
        else if (argument == "--help")
        {
            std::cout << "Usage: fastdds_bridge [--config FILE] [--validate-config]\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << argument << '\n';
            return 2;
        }
    }

    try
    {
        const auto config = load_config(config_path);
        DdsTopicRegistry registry(config);
        for (const auto& topic : registry.topics())
        {
            std::cout << "Loaded " << topic->config.dds_topic << " as "
                      << topic->config.rosbridge_topic << " ("
                      << topic->config.wire_type << ")\n";
        }
        if (validate_only)
        {
            return 0;
        }
        RosbridgeProtocol protocol(registry);
        WebSocketServer server(protocol);
        if (!server.start(config.host, config.port))
        {
            throw std::runtime_error("could not listen on " + config.host + ":" + std::to_string(config.port));
        }
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        std::cout << "Rosbridge WebSocket listening on ws://" << config.host << ':' << config.port << '\n';
        while (running)
        {
            for (const auto& sample : registry.take_samples())
            {
                server.publish(sample);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        server.stop();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FastDDS-Bridge: " << error.what() << '\n';
        return 1;
    }
}
