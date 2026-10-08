#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct TopicConfig
{
    std::string dds_topic;
    std::filesystem::path idl_path;
    std::string type_name;
    std::string rosbridge_topic;
    std::string wire_type;
    bool allow_publish{false};
};

struct BridgeConfig
{
    std::string host{"0.0.0.0"};
    uint16_t port{9090};
    uint32_t domain_id{0};
    std::vector<TopicConfig> topics;
};

BridgeConfig load_config(const std::filesystem::path& config_path);
