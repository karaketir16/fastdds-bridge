#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct BridgeConfig
{
    std::string host{"0.0.0.0"};
    uint16_t port{9090};
    uint32_t domain_id{0};
    std::vector<std::filesystem::path> idl_files;
};

BridgeConfig parse_arguments(int argc, char** argv);
