#pragma once

#include "config.hpp"

#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/builtin/topic/PublicationBuiltinTopicData.hpp>
#include <fastdds/dds/builtin/topic/SubscriptionBuiltinTopicData.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicType.hpp>
#include <nlohmann/json.hpp>

struct TopicConfig
{
    std::string dds_topic;
    std::string type_name;
    std::string rosbridge_topic;
    std::string wire_type;
};

struct IdlTypeRuntime
{
    std::string type_name;
    std::string wire_type;
    eprosima::fastdds::dds::DynamicType::_ref_type dynamic_type;
    eprosima::fastdds::dds::TypeSupport type_support;
    std::vector<nlohmann::json> type_definitions;
};

struct TopicRuntime
{
    TopicConfig config;
    std::shared_ptr<IdlTypeRuntime> idl_type;
    eprosima::fastdds::dds::DynamicType::_ref_type dynamic_type;
    eprosima::fastdds::dds::TypeSupport type_support;
    eprosima::fastdds::dds::Topic* topic{nullptr};
    eprosima::fastdds::dds::Publisher* publisher{nullptr};
    eprosima::fastdds::dds::Subscriber* subscriber{nullptr};
    eprosima::fastdds::dds::DataWriter* writer{nullptr};
    eprosima::fastdds::dds::DataReader* reader{nullptr};
    std::vector<nlohmann::json> type_definitions;
};

struct DiscoveredEndpoint
{
    std::string topic_name;
    std::string type_name;
};

class BridgeDiscoveryListener : public eprosima::fastdds::dds::DomainParticipantListener
{
public:
    void on_data_reader_discovery(
            eprosima::fastdds::dds::DomainParticipant* participant,
            eprosima::fastdds::rtps::ReaderDiscoveryStatus reason,
            const eprosima::fastdds::dds::SubscriptionBuiltinTopicData& info,
            bool& should_be_ignored) override;
    void on_data_writer_discovery(
            eprosima::fastdds::dds::DomainParticipant* participant,
            eprosima::fastdds::rtps::WriterDiscoveryStatus reason,
            const eprosima::fastdds::dds::PublicationBuiltinTopicData& info,
            bool& should_be_ignored) override;
    std::vector<DiscoveredEndpoint> take_discoveries();

private:
    void enqueue(const char* topic, const char* type);
    std::mutex mutex_;
    std::vector<DiscoveredEndpoint> discoveries_;
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

    std::vector<TopicRuntime*> topics() const;
    TopicRuntime* find_topic(const std::string& rosbridge_topic) const;
    TopicRuntime* find_type(const std::string& wire_type) const;
    TopicRuntime* add_topic(const std::string& dds_topic, const std::string& type_name);
    void process_discoveries();
    std::vector<PublishedSample> take_samples();
    void write_json(TopicRuntime& topic, const nlohmann::json& message);

private:
    void release_entities() noexcept;
    eprosima::fastdds::dds::DomainParticipant* participant_{nullptr};
    std::unique_ptr<BridgeDiscoveryListener> discovery_listener_;
    std::map<std::string, std::shared_ptr<IdlTypeRuntime>> idl_types_;
    mutable std::mutex topics_mutex_;
    std::vector<std::unique_ptr<TopicRuntime>> topics_;
};

nlohmann::json type_definitions_for(
        const eprosima::fastdds::dds::DynamicType::_ref_type& root_type,
        const std::string& root_wire_type);
