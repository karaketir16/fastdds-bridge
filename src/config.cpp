#include "config.hpp"

#include <set>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace
{
template<typename T>
T required(const YAML::Node& node, const char* key, const std::string& context)
{
    const auto value = node[key];
    if (!value)
    {
        throw std::runtime_error(context + " is missing required field '" + key + "'");
    }
    try
    {
        return value.as<T>();
    }
    catch (const YAML::Exception& error)
    {
        throw std::runtime_error(context + " has invalid field '" + key + "': " + error.what());
    }
}

std::string default_wire_type(const std::string& idl_type)
{
    std::string result;
    for (std::size_t i = 0; i < idl_type.size(); ++i)
    {
        if (idl_type[i] == ':' && i + 1 < idl_type.size() && idl_type[i + 1] == ':')
        {
            result.push_back('/');
            ++i;
        }
        else
        {
            result.push_back(idl_type[i]);
        }
    }
    return result;
}
}

BridgeConfig load_config(const std::filesystem::path& config_path)
{
    const auto absolute_config = std::filesystem::absolute(config_path);
    if (!std::filesystem::is_regular_file(absolute_config))
    {
        throw std::runtime_error("configuration file does not exist: " + absolute_config.string());
    }

    YAML::Node root;
    try
    {
        root = YAML::LoadFile(absolute_config.string());
    }
    catch (const YAML::Exception& error)
    {
        throw std::runtime_error("cannot parse configuration file: " + std::string(error.what()));
    }

    BridgeConfig config;
    const auto server = root["server"];
    if (!server || !server.IsMap())
    {
        throw std::runtime_error("configuration requires a 'server' mapping");
    }
    if (server["host"])
    {
        config.host = server["host"].as<std::string>();
    }
    if (server["port"])
    {
        config.port = server["port"].as<uint16_t>();
        if (config.port == 0)
        {
            throw std::runtime_error("server.port must be between 1 and 65535");
        }
    }
    if (server["domain_id"])
    {
        config.domain_id = server["domain_id"].as<uint32_t>();
    }

    const auto topics = root["topics"];
    if (!topics || !topics.IsSequence() || topics.size() == 0)
    {
        throw std::runtime_error("configuration requires a non-empty 'topics' sequence");
    }

    std::set<std::string> dds_topics;
    std::set<std::string> exposed_topics;
    for (std::size_t i = 0; i < topics.size(); ++i)
    {
        const auto entry = topics[i];
        const auto context = "topics[" + std::to_string(i) + "]";
        if (!entry.IsMap())
        {
            throw std::runtime_error(context + " must be a mapping");
        }

        TopicConfig topic;
        topic.dds_topic = required<std::string>(entry, "dds_topic", context);
        const auto idl = required<std::string>(entry, "idl", context);
        topic.idl_path = (absolute_config.parent_path() / idl).lexically_normal();
        topic.type_name = required<std::string>(entry, "type_name", context);
        topic.rosbridge_topic = required<std::string>(entry, "rosbridge_topic", context);
        topic.wire_type = entry["wire_type"] ? entry["wire_type"].as<std::string>() : default_wire_type(topic.type_name);
        topic.allow_publish = entry["allow_publish"] ? entry["allow_publish"].as<bool>() : false;

        if (topic.dds_topic.empty() || topic.type_name.empty() || topic.rosbridge_topic.empty() || topic.wire_type.empty())
        {
            throw std::runtime_error(context + " contains an empty topic or type name");
        }
        if (topic.rosbridge_topic.front() != '/')
        {
            throw std::runtime_error(context + " rosbridge_topic must start with '/'");
        }
        if (!std::filesystem::is_regular_file(topic.idl_path))
        {
            throw std::runtime_error(context + " IDL file does not exist: " + topic.idl_path.string());
        }
        if (!dds_topics.insert(topic.dds_topic).second)
        {
            throw std::runtime_error("duplicate DDS topic in configuration: " + topic.dds_topic);
        }
        if (!exposed_topics.insert(topic.rosbridge_topic).second)
        {
            throw std::runtime_error("duplicate Rosbridge topic in configuration: " + topic.rosbridge_topic);
        }
        config.topics.push_back(std::move(topic));
    }

    return config;
}
