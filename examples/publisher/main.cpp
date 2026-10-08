#include "config.hpp"
#include "dds_topic.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
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
    if (argc == 3 && std::string(argv[1]) == "--config")
    {
        config_path = argv[2];
    }
    else if (argc != 1)
    {
        std::cerr << "Usage: fastdds_bridge_example_publisher [--config FILE]\n";
        return 2;
    }

    try
    {
        const auto config = load_config(config_path);
        DdsTopicRegistry registry(config);
        auto* telemetry = registry.find_topic("/demo/telemetry");
        auto* path = registry.find_topic("/demo/path");
        if (telemetry == nullptr || path == nullptr)
        {
            throw std::runtime_error("example publisher requires /demo/telemetry and /demo/path in the config");
        }

        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        uint32_t sequence = 0;
        while (running)
        {
            registry.write_json(*telemetry, {
                {"sample_sequence", sequence},
                {"value", 42.0 + (sequence % 10)},
                {"label", "Fast DDS telemetry"},
                {"active", true},
            });
            registry.write_json(*path, {
                {"sample_sequence", sequence},
                {"path", nlohmann::json::array({
                    {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}},
                    {{"x", 1.0}, {"y", 0.5}, {"z", 0.25}},
                    {{"x", 2.0}, {"y", 1.0}, {"z", 0.5}},
                })},
                {"status", "sample path"},
            });
            std::cout << "Published sample " << sequence++ << '\n';
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "FastDDS-Bridge example publisher: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
