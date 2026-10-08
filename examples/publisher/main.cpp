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
    try
    {
        const auto config = parse_arguments(argc, argv);
        DdsTopicRegistry registry(config);
        auto* telemetry = registry.add_topic("DemoTelemetry", "demo::msg::Telemetry");
        auto* path = registry.add_topic("DemoPoseSequence", "demo::msg::PoseSequence");
        auto* vehicle = registry.add_topic("DemoVehicleState", "demo::msg::VehicleState");
        if (telemetry == nullptr || path == nullptr || vehicle == nullptr)
        {
            throw std::runtime_error(
                      "sample IDL files must define demo::msg::Telemetry, demo::msg::PoseSequence, and demo::msg::VehicleState");
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
            registry.write_json(*vehicle, {
                {"device_id", 101},
                {"name", "rover-alpha"},
                {"mode", "demo::msg::AUTONOMOUS"},
                {"position", {{"x", 12.5 + sequence}, {"y", 3.0}, {"z", 0.0}}},
                {"route", nlohmann::json::array({
                    {{"x", 13.0 + sequence}, {"y", 3.0}, {"speed", 1.5}},
                    {{"x", 14.0 + sequence}, {"y", 4.0}, {"speed", 0.75}},
                })},
                {"covariance", nlohmann::json::array({0.1, 0.0, 0.1})},
            });
            registry.write_json(*vehicle, {
                {"device_id", 202},
                {"name", "rover-beta"},
                {"mode", "demo::msg::MANUAL"},
                {"position", {{"x", -2.0}, {"y", 8.0 + sequence}, {"z", 0.5}}},
                {"route", nlohmann::json::array({
                    {{"x", -1.0}, {"y", 8.0 + sequence}, {"speed", 0.5}},
                })},
                {"covariance", nlohmann::json::array({0.2, 0.01, 0.2})},
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
