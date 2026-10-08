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
    try
    {
        const auto config = parse_arguments(argc, argv);
        DdsTopicRegistry registry(config);
        std::cout << "Loaded " << config.idl_files.size() << " IDL file(s); waiting for matching DDS topics\n";
        for (const auto& idl : config.idl_files)
        {
            std::cout << "  " << idl.string() << '\n';
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
            registry.process_discoveries();
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
