#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace
{
void append_idl_path(BridgeConfig& config, const std::filesystem::path& input)
{
    const auto path = std::filesystem::absolute(input).lexically_normal();
    if (!std::filesystem::exists(path))
    {
        throw std::runtime_error("IDL path does not exist: " + path.string());
    }
    if (std::filesystem::is_regular_file(path))
    {
        if (path.extension() != ".idl")
        {
            throw std::runtime_error("IDL input must have a .idl extension: " + path.string());
        }
        config.idl_files.push_back(path);
        return;
    }
    if (!std::filesystem::is_directory(path))
    {
        throw std::runtime_error("IDL input is neither a file nor a directory: " + path.string());
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(path))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".idl")
        {
            config.idl_files.push_back(std::filesystem::absolute(entry.path()).lexically_normal());
        }
    }
}

template<typename T>
T parse_number(const std::string& value, const std::string& flag)
{
    try
    {
        std::size_t parsed = 0;
        const auto number = std::stoull(value, &parsed);
        if (parsed != value.size() || number > static_cast<unsigned long long>(std::numeric_limits<T>::max()))
        {
            throw std::out_of_range("value out of range");
        }
        return static_cast<T>(number);
    }
    catch (const std::exception&)
    {
        throw std::runtime_error("invalid value for " + flag + ": " + value);
    }
}

void load_topic_rates(BridgeConfig& config, const std::filesystem::path& file)
{
    std::ifstream input(file);
    if (!input)
    {
        throw std::runtime_error("could not open config file: " + file.string());
    }

    nlohmann::json document;
    try
    {
        input >> document;
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error("could not parse config file '" + file.string() + "': " + error.what());
    }

    if (!document.is_object())
    {
        throw std::runtime_error("config file must contain a JSON object");
    }
    for (auto it = document.begin(); it != document.end(); ++it)
    {
        if (it.key() != "topic_rates_hz")
        {
            throw std::runtime_error("unknown config setting: " + it.key());
        }
    }
    if (!document.contains("topic_rates_hz"))
    {
        return;
    }

    const auto& topic_rates = document.at("topic_rates_hz");
    if (!topic_rates.is_object())
    {
        throw std::runtime_error("topic_rates_hz must be a JSON object mapping topic names to Hz values");
    }
    for (auto it = topic_rates.begin(); it != topic_rates.end(); ++it)
    {
        if (it.key().empty() || it.key().front() != '/')
        {
            throw std::runtime_error("topic_rates_hz keys must be Rosbridge topic names starting with '/'");
        }
        if (!it.value().is_number())
        {
            throw std::runtime_error("rate for topic '" + it.key() + "' must be a positive number of Hz");
        }
        const auto rate = it.value().get<double>();
        if (!std::isfinite(rate) || rate <= 0.0)
        {
            throw std::runtime_error("rate for topic '" + it.key() + "' must be a positive finite number of Hz");
        }
        config.topic_rates_hz[it.key()] = rate;
    }
}
}

BridgeConfig parse_arguments(int argc, char** argv)
{
    BridgeConfig config;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--idl" && i + 1 < argc)
        {
            append_idl_path(config, argv[++i]);
        }
        else if (argument == "--host" && i + 1 < argc)
        {
            config.host = argv[++i];
        }
        else if (argument == "--port" && i + 1 < argc)
        {
            config.port = parse_number<uint16_t>(argv[++i], argument);
            if (config.port == 0)
            {
                throw std::runtime_error("--port must be between 1 and 65535");
            }
        }
        else if (argument == "--domain" && i + 1 < argc)
        {
            config.domain_id = parse_number<uint32_t>(argv[++i], argument);
        }
        else if (argument == "--config" && i + 1 < argc)
        {
            load_topic_rates(config, argv[++i]);
        }
        else if (argument == "--help")
        {
            std::cout << "Usage: fastdds_bridge --idl FILE_OR_DIRECTORY [--idl FILE_OR_DIRECTORY ...] "
                         "[--config CONFIG.json] [--domain ID] [--host HOST] [--port PORT]\n";
            std::exit(0);
        }
        else
        {
            throw std::runtime_error("unknown or incomplete argument: " + argument);
        }
    }

    std::sort(config.idl_files.begin(), config.idl_files.end());
    config.idl_files.erase(std::unique(config.idl_files.begin(), config.idl_files.end()), config.idl_files.end());
    if (config.idl_files.empty())
    {
        throw std::runtime_error("provide at least one IDL file or directory with --idl");
    }
    return config;
}
