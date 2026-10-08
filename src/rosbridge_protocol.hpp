#pragma once

#include "dds_topic.hpp"

#include <map>
#include <mutex>
#include <string>
#include <vector>

class RosbridgeProtocol
{
public:
    explicit RosbridgeProtocol(DdsTopicRegistry& registry);

    std::vector<nlohmann::json> handle(const std::string& client_id, const nlohmann::json& request);
    void disconnect(const std::string& client_id);
    std::vector<std::string> subscribers(const std::string& topic) const;
    std::string compression(const std::string& client_id, const std::string& topic) const;

private:
    nlohmann::json status(const std::string& level, const std::string& message, const nlohmann::json& request) const;
    nlohmann::json call_service(const nlohmann::json& request);

    DdsTopicRegistry& registry_;
    mutable std::mutex mutex_;
    std::map<std::string, std::map<std::string, std::string>> subscriptions_;
    std::map<std::string, std::map<std::string, std::string>> subscription_compression_;
    std::map<std::string, std::map<std::string, std::string>> advertised_;
};
