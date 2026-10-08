#include "rosbridge_protocol.hpp"

#include <stdexcept>

namespace
{
std::string raw_message_definition(const nlohmann::json& typedefs)
{
    std::string result;
    for (std::size_t type_index = 0; type_index < typedefs.size(); ++type_index)
    {
        const auto& definition = typedefs[type_index];
        if (type_index != 0)
        {
            result += "\n================================================================================\nMSG: ";
            result += definition.value("type", "");
            result += "\n";
        }
        const auto& names = definition.at("fieldnames");
        const auto& types = definition.at("fieldtypes");
        const auto& lengths = definition.at("fieldarraylen");
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (i != 0) result += "\n";
            result += types.at(i).get<std::string>();
            const auto length = lengths.at(i).get<int32_t>();
            if (length == 0) result += "[]";
            else if (length > 0) result += "[" + std::to_string(length) + "]";
            result += " ";
            result += names.at(i).get<std::string>();
        }
        result += "\n";
    }
    return result;
}
}

RosbridgeProtocol::RosbridgeProtocol(DdsTopicRegistry& registry)
    : registry_(registry)
{
}

nlohmann::json RosbridgeProtocol::status(
        const std::string& level,
        const std::string& message,
        const nlohmann::json& request) const
{
    nlohmann::json result = {{"op", "status"}, {"level", level}, {"msg", message}};
    if (request.contains("id"))
    {
        result["id"] = request["id"];
    }
    return result;
}

nlohmann::json RosbridgeProtocol::call_service(const nlohmann::json& request)
{
    const auto service = request.value("service", "");
    const auto id = request.value("id", "");
    const auto args = request.value("args", nlohmann::json::object());
    nlohmann::json values;

    if (service == "/rosapi/topics")
    {
        values = {{"topics", nlohmann::json::array()}, {"types", nlohmann::json::array()}};
        for (const auto& topic : registry_.topics())
        {
            values["topics"].push_back(topic->config.rosbridge_topic);
            values["types"].push_back(topic->config.wire_type);
        }
    }
    else if (service == "/rosapi/topics_and_raw_types")
    {
        values = { {"topics", nlohmann::json::array()}, {"types", nlohmann::json::array()},
            {"typedefs_full_text", nlohmann::json::array()} };
        for (const auto& topic : registry_.topics())
        {
            values["topics"].push_back(topic->config.rosbridge_topic);
            values["types"].push_back(topic->config.wire_type);
            values["typedefs_full_text"].push_back(raw_message_definition(topic->type_definitions));
        }
        // Lichtblick detects ROS 2 by checking for this standard interface type.
        // The player iterates topics, so this capability marker stays out of the visible topic list.
        values["types"].push_back("rcl_interfaces/msg/Log");
    }
    else if (service == "/rosapi/get_ros_version")
    {
        values = {{"version", 2}, {"distro", "fastdds"}};
    }
    else if (service == "/rosapi/nodes")
    {
        values = {{"nodes", nlohmann::json::array()}};
    }
    else if (service == "/rosapi/topic_type")
    {
        const auto topic_name = args.value("topic", "");
        auto* topic = registry_.find_topic(topic_name);
        if (topic == nullptr)
        {
            values = {{"type", ""}};
        }
        else
        {
            values = {{"type", topic->config.wire_type}};
        }
    }
    else if (service == "/rosapi/message_details")
    {
        const auto type_name = args.value("type", "");
        auto* topic = registry_.find_type(type_name);
        nlohmann::json definitions = nlohmann::json::array();
        if (topic != nullptr) definitions = topic->type_definitions;
        values = {{"typedefs", std::move(definitions)}};
    }
    else
    {
        return {{"op", "service_response"}, {"service", service}, {"id", id},
            {"result", false}, {"values", nlohmann::json::object()}};
    }

    return {{"op", "service_response"}, {"service", service}, {"id", id}, {"result", true}, {"values", values}};
}

std::vector<nlohmann::json> RosbridgeProtocol::handle(const std::string& client_id, const nlohmann::json& request)
{
    std::vector<nlohmann::json> responses;
    try
    {
        if (!request.is_object() || !request.contains("op") || !request["op"].is_string())
        {
            return {status("error", "request must be an object with a string op", request)};
        }
        const auto op = request["op"].get<std::string>();
        if (op == "call_service")
        {
            return {call_service(request)};
        }
        if (op == "subscribe")
        {
            const auto topic_name = request.value("topic", "");
            auto* topic = registry_.find_topic(topic_name);
            if (topic == nullptr)
            {
                return {status("error", "topic is not in the bridge allowlist: " + topic_name, request)};
            }
            const auto type = request.value("type", topic->config.wire_type);
            if (type != topic->config.wire_type)
            {
                return {status("error", "type does not match configured topic type", request)};
            }
            std::lock_guard<std::mutex> lock(mutex_);
            subscriptions_[client_id][topic_name] = type;
            subscription_compression_[client_id][topic_name] = request.value("compression", "none");
            return {};
        }
        if (op == "unsubscribe")
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto topic_name = request.value("topic", "");
            auto client = subscriptions_.find(client_id);
            if (client != subscriptions_.end())
            {
                client->second.erase(topic_name);
                if (client->second.empty()) subscriptions_.erase(client);
            }
            auto compression_client = subscription_compression_.find(client_id);
            if (compression_client != subscription_compression_.end())
            {
                compression_client->second.erase(topic_name);
                if (compression_client->second.empty()) subscription_compression_.erase(compression_client);
            }
            return {};
        }
        if (op == "advertise")
        {
            const auto topic_name = request.value("topic", "");
            auto* topic = registry_.find_topic(topic_name);
            if (topic == nullptr || !topic->config.allow_publish)
            {
                return {status("error", "topic is not configured for publishing: " + topic_name, request)};
            }
            const auto type = request.value("type", "");
            if (type != topic->config.wire_type)
            {
                return {status("error", "type does not match configured topic type", request)};
            }
            std::lock_guard<std::mutex> lock(mutex_);
            advertised_[client_id][topic_name] = type;
            return {};
        }
        if (op == "unadvertise")
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto topic_name = request.value("topic", "");
            auto client = advertised_.find(client_id);
            if (client != advertised_.end())
            {
                client->second.erase(topic_name);
                if (client->second.empty()) advertised_.erase(client);
            }
            return {};
        }
        if (op == "publish")
        {
            const auto topic_name = request.value("topic", "");
            auto* topic = registry_.find_topic(topic_name);
            if (topic == nullptr || !topic->config.allow_publish)
            {
                return {status("error", "topic is not configured for publishing: " + topic_name, request)};
            }
            if (request.contains("type") && request["type"] != topic->config.wire_type)
            {
                return {status("error", "type does not match configured topic type", request)};
            }
            if (!request.contains("msg") || !request["msg"].is_object())
            {
                return {status("error", "publish requires an object msg", request)};
            }
            registry_.write_json(*topic, request["msg"]);
            return {};
        }
        return {status("error", "unsupported Rosbridge operation: " + op, request)};
    }
    catch (const std::exception& error)
    {
        return {status("error", error.what(), request)};
    }
}

void RosbridgeProtocol::disconnect(const std::string& client_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_.erase(client_id);
    subscription_compression_.erase(client_id);
    advertised_.erase(client_id);
}

std::string RosbridgeProtocol::compression(const std::string& client_id, const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto client = subscription_compression_.find(client_id);
    if (client == subscription_compression_.end()) return "none";
    const auto value = client->second.find(topic);
    return value == client->second.end() ? "none" : value->second;
}

std::vector<std::string> RosbridgeProtocol::subscribers(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (const auto& client : subscriptions_)
    {
        if (client.second.find(topic) != client.second.end()) result.push_back(client.first);
    }
    return result;
}
