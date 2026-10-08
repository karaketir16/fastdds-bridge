#pragma once

#include "config.hpp"

#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicType.hpp>
#include <nlohmann/json.hpp>

struct TopicRuntime
{
    TopicConfig config;
    eprosima::fastdds::dds::DynamicType::_ref_type dynamic_type;
    eprosima::fastdds::dds::TypeSupport type_support;
    eprosima::fastdds::dds::Topic* topic{nullptr};
    eprosima::fastdds::dds::Publisher* publisher{nullptr};
    eprosima::fastdds::dds::Subscriber* subscriber{nullptr};
    eprosima::fastdds::dds::DataWriter* writer{nullptr};
    eprosima::fastdds::dds::DataReader* reader{nullptr};
    std::vector<nlohmann::json> type_definitions;
};

struct PublishedSample
{
    std::string topic;
    std::string wire_type;
    nlohmann::json message;
    std::vector<uint8_t> serialized_ros2;
};

class DdsTopicRegistry
{
public:
    explicit DdsTopicRegistry(const BridgeConfig& config);
    ~DdsTopicRegistry();

    DdsTopicRegistry(const DdsTopicRegistry&) = delete;
    DdsTopicRegistry& operator=(const DdsTopicRegistry&) = delete;

    const std::vector<std::unique_ptr<TopicRuntime>>& topics() const noexcept;
    TopicRuntime* find_topic(const std::string& rosbridge_topic) const noexcept;
    TopicRuntime* find_type(const std::string& wire_type) const noexcept;
    std::vector<PublishedSample> take_samples();
    void write_json(TopicRuntime& topic, const nlohmann::json& message);

private:
    void release_entities() noexcept;
    eprosima::fastdds::dds::DomainParticipant* participant_{nullptr};
    std::vector<std::unique_ptr<TopicRuntime>> topics_;
};

nlohmann::json type_definitions_for(
        const eprosima::fastdds::dds::DynamicType::_ref_type& root_type,
        const std::string& root_wire_type);
