#include "dds_topic.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

#include <fastdds/dds/xtypes/utils.hpp>

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/subscriber/qos/SubscriberQos.hpp>
#include <fastdds/dds/topic/qos/TopicQos.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicPubSubType.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicDataFactory.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicTypeBuilderFactory.hpp>
#include <fastdds/dds/xtypes/dynamic_types/MemberDescriptor.hpp>
#include <fastdds/dds/xtypes/dynamic_types/TypeDescriptor.hpp>
#include <fastdds/dds/xtypes/dynamic_types/Types.hpp>
#include <fastdds/rtps/common/SerializedPayload.hpp>

using namespace eprosima::fastdds::dds;

namespace
{
std::string as_string(const ObjectName& name)
{
    return std::string(name.c_str());
}

std::string primitive_name(TypeKind kind)
{
    switch (kind)
    {
        case TK_BOOLEAN: return "bool";
        case TK_BYTE:
        case TK_UINT8: return "uint8";
        case TK_INT8: return "int8";
        case TK_INT16: return "int16";
        case TK_UINT16: return "uint16";
        case TK_INT32: return "int32";
        case TK_UINT32: return "uint32";
        case TK_INT64: return "int64";
        case TK_UINT64: return "uint64";
        case TK_FLOAT32: return "float32";
        case TK_FLOAT64: return "float64";
        case TK_FLOAT128: return "float128";
        case TK_CHAR8: return "char";
        case TK_CHAR16: return "wchar";
        case TK_STRING8: return "string";
        case TK_STRING16: return "wstring";
        default: return {};
    }
}

std::string wire_name(const DynamicType::_ref_type& type)
{
    std::string name = as_string(type->get_name());
    std::string result;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        if (name[i] == ':' && i + 1 < name.size() && name[i + 1] == ':')
        {
            result.push_back('/');
            ++i;
        }
        else
        {
            result.push_back(name[i]);
        }
    }
    return result;
}

std::string field_type_name(const DynamicType::_ref_type& type, const std::string& nested_wire_prefix)
{
    traits<TypeDescriptor>::ref_type descriptor = traits<TypeDescriptor>::make_shared();
    if (type->get_descriptor(descriptor) != RETCODE_OK)
    {
        throw std::runtime_error("could not inspect IDL member type");
    }
    const auto kind = descriptor->kind();
    const auto primitive = primitive_name(kind);
    if (!primitive.empty())
    {
        return primitive;
    }
    if (kind == TK_ALIAS)
    {
        return field_type_name(descriptor->base_type(), nested_wire_prefix);
    }
    if (kind == TK_SEQUENCE || kind == TK_ARRAY)
    {
        return field_type_name(descriptor->element_type(), nested_wire_prefix);
    }
    auto name = wire_name(type);
    if (!nested_wire_prefix.empty())
    {
        const auto last_separator = name.find_last_of('/');
        name = nested_wire_prefix + "/" +
                (last_separator == std::string::npos ? name : name.substr(last_separator + 1));
    }
    return name;
}

int32_t array_length(const DynamicType::_ref_type& type)
{
    traits<TypeDescriptor>::ref_type descriptor = traits<TypeDescriptor>::make_shared();
    if (type->get_descriptor(descriptor) != RETCODE_OK)
    {
        throw std::runtime_error("could not inspect IDL array member");
    }
    if (descriptor->kind() == TK_SEQUENCE)
    {
        return 0;
    }
    if (descriptor->kind() == TK_ARRAY)
    {
        const auto& bounds = descriptor->bound();
        if (!bounds.empty() && bounds.front() <= static_cast<uint32_t>(INT32_MAX))
        {
            return static_cast<int32_t>(bounds.front());
        }
        return 0;
    }
    return -1;
}

void collect_type_definitions(
        const DynamicType::_ref_type& type,
        const std::string& root_wire_type,
        const std::string& nested_wire_prefix,
        std::set<std::string>& visited,
        std::vector<nlohmann::json>& definitions)
{
    const auto type_name = type == nullptr ? std::string{} : wire_name(type);
    const bool is_root = visited.empty();
    auto nested_name = type_name;
    if (!nested_wire_prefix.empty())
    {
        const auto last_separator = nested_name.find_last_of('/');
        nested_name = nested_wire_prefix + "/" +
                (last_separator == std::string::npos ? nested_name : nested_name.substr(last_separator + 1));
    }
    const auto wire_type = visited.empty() && !root_wire_type.empty() ? root_wire_type : nested_name;
    if (visited.find(wire_type) != visited.end())
    {
        return;
    }
    visited.insert(wire_type);

    nlohmann::json definition = {
        {"type", wire_type},
        {"fieldnames", nlohmann::json::array()},
        {"fieldtypes", nlohmann::json::array()},
        {"fieldarraylen", nlohmann::json::array()},
        {"examples", nlohmann::json::array()},
        {"constnames", nlohmann::json::array()},
        {"constvalues", nlohmann::json::array()},
    };

    if (type->get_kind() == TK_STRUCTURE || type->get_kind() == TK_UNION)
    {
        for (uint32_t i = 0; i < type->get_member_count(); ++i)
        {
            DynamicTypeMember::_ref_type member;
            if (type->get_member_by_index(member, i) != RETCODE_OK)
            {
                throw std::runtime_error("could not inspect IDL type member");
            }
            traits<MemberDescriptor>::ref_type descriptor = traits<MemberDescriptor>::make_shared();
            if (member->get_descriptor(descriptor) != RETCODE_OK)
            {
                throw std::runtime_error("could not inspect IDL member descriptor");
            }
            const auto member_type = descriptor->type();
            const auto member_kind = member_type->get_kind();
            definition["fieldnames"].push_back(as_string(member->get_name()));
            definition["fieldtypes"].push_back(field_type_name(member_type, nested_wire_prefix));
            definition["fieldarraylen"].push_back(array_length(member_type));
            if (array_length(member_type) >= 0)
            {
                definition["examples"].push_back("[]");
            }
            else if (member_kind == TK_STRUCTURE || member_kind == TK_UNION)
            {
                definition["examples"].push_back("{}");
            }
            else if (member_kind == TK_STRING8 || member_kind == TK_STRING16)
            {
                definition["examples"].push_back("");
            }
            else
            {
                definition["examples"].push_back("0");
            }

            DynamicType::_ref_type nested = member_type;
            if (member_kind == TK_SEQUENCE || member_kind == TK_ARRAY)
            {
                traits<TypeDescriptor>::ref_type sequence_descriptor = traits<TypeDescriptor>::make_shared();
                if (member_type->get_descriptor(sequence_descriptor) == RETCODE_OK)
                {
                    nested = sequence_descriptor->element_type();
                }
            }
            if (nested && (nested->get_kind() == TK_STRUCTURE || nested->get_kind() == TK_UNION || nested->get_kind() == TK_ENUM))
            {
                collect_type_definitions(nested, {}, nested_wire_prefix, visited, definitions);
            }
        }
    }
    else if (type->get_kind() == TK_ENUM)
    {
        for (uint32_t i = 0; i < type->get_member_count(); ++i)
        {
            DynamicTypeMember::_ref_type member;
            traits<MemberDescriptor>::ref_type descriptor = traits<MemberDescriptor>::make_shared();
            if (type->get_member_by_index(member, i) == RETCODE_OK && member->get_descriptor(descriptor) == RETCODE_OK)
            {
                definition["constnames"].push_back(as_string(member->get_name()));
                definition["constvalues"].push_back(descriptor->literal_value());
            }
        }
    }

    if (is_root)
    {
        definitions.insert(definitions.begin(), std::move(definition));
    }
    else
    {
        definitions.push_back(std::move(definition));
    }
}
}

nlohmann::json type_definitions_for(const DynamicType::_ref_type& root_type, const std::string& root_wire_type)
{
    std::set<std::string> visited;
    std::vector<nlohmann::json> definitions;
    const auto separator = root_wire_type.find_last_of('/');
    const auto nested_wire_prefix = separator == std::string::npos ? std::string{} : root_wire_type.substr(0, separator);
    collect_type_definitions(root_type, root_wire_type, nested_wire_prefix, visited, definitions);
    return definitions;
}

DdsTopicRegistry::DdsTopicRegistry(const BridgeConfig& config)
{
    participant_ = DomainParticipantFactory::get_instance()->create_participant(config.domain_id, PARTICIPANT_QOS_DEFAULT);
    if (participant_ == nullptr)
    {
        throw std::runtime_error("Fast DDS could not create a DomainParticipant");
    }

    try
    {
        auto factory = DynamicTypeBuilderFactory::get_instance();
        for (const auto& topic_config : config.topics)
        {
            topics_.push_back(std::make_unique<TopicRuntime>());
            auto& runtime = *topics_.back();
            runtime.config = topic_config;

            const IncludePathSeq include_paths{topic_config.idl_path.parent_path().string()};
            DynamicTypeBuilder::_ref_type selected_builder;
            const auto parse_result = factory->for_each_type_w_uri(
                    topic_config.idl_path.string(), include_paths,
                    [&](DynamicTypeBuilder::_ref_type candidate)
                    {
                        if (candidate && candidate->get_name() == topic_config.type_name)
                        {
                            selected_builder = candidate;
                        }
                        return true;
                    });
            if (parse_result != RETCODE_OK)
            {
                throw std::runtime_error("Fast DDS could not parse IDL file " + topic_config.idl_path.string());
            }
            if (selected_builder == nullptr)
            {
                throw std::runtime_error("Fast DDS could not load type '" + topic_config.type_name + "' from " +
                        topic_config.idl_path.string() + " (the IDL may be invalid or the type name may be absent)");
            }
            runtime.dynamic_type = selected_builder->build();
            if (runtime.dynamic_type == nullptr)
            {
                throw std::runtime_error("Fast DDS could not build dynamic type '" + topic_config.type_name + "'");
            }

            runtime.type_support = TypeSupport(new DynamicPubSubType(runtime.dynamic_type));
            if (runtime.type_support.register_type(participant_) != RETCODE_OK)
            {
                throw std::runtime_error("Fast DDS could not register type '" + topic_config.type_name + "'");
            }
            runtime.topic = participant_->create_topic(topic_config.dds_topic, runtime.type_support.get_type_name(), TOPIC_QOS_DEFAULT);
            if (runtime.topic == nullptr)
            {
                throw std::runtime_error("Fast DDS could not create DDS topic '" + topic_config.dds_topic + "'");
            }
            runtime.publisher = participant_->create_publisher(PUBLISHER_QOS_DEFAULT, nullptr);
            runtime.subscriber = participant_->create_subscriber(SUBSCRIBER_QOS_DEFAULT, nullptr);
            if (runtime.publisher == nullptr || runtime.subscriber == nullptr)
            {
                throw std::runtime_error("Fast DDS could not create topic publisher/subscriber for '" + topic_config.dds_topic + "'");
            }
            runtime.writer = runtime.publisher->create_datawriter(runtime.topic, DATAWRITER_QOS_DEFAULT, nullptr);
            runtime.reader = runtime.subscriber->create_datareader(runtime.topic, DATAREADER_QOS_DEFAULT, nullptr);
            if (runtime.writer == nullptr || runtime.reader == nullptr)
            {
                throw std::runtime_error("Fast DDS could not create topic reader/writer for '" + topic_config.dds_topic + "'");
            }
            runtime.type_definitions = type_definitions_for(runtime.dynamic_type, topic_config.wire_type);
        }
    }
    catch (...)
    {
        release_entities();
        throw;
    }
}

DdsTopicRegistry::~DdsTopicRegistry()
{
    release_entities();
}

void DdsTopicRegistry::release_entities() noexcept
{
    if (participant_ != nullptr)
    {
        for (auto& topic : topics_)
        {
            if (topic->reader != nullptr && topic->subscriber != nullptr)
            {
                topic->subscriber->delete_datareader(topic->reader);
                topic->reader = nullptr;
            }
            if (topic->writer != nullptr && topic->publisher != nullptr)
            {
                topic->publisher->delete_datawriter(topic->writer);
                topic->writer = nullptr;
            }
            if (topic->subscriber != nullptr)
            {
                participant_->delete_subscriber(topic->subscriber);
            }
            if (topic->publisher != nullptr)
            {
                participant_->delete_publisher(topic->publisher);
            }
            if (topic->topic != nullptr)
            {
                participant_->delete_topic(topic->topic);
            }
        }
        DomainParticipantFactory::get_instance()->delete_participant(participant_);
        participant_ = nullptr;
    }
}

const std::vector<std::unique_ptr<TopicRuntime>>& DdsTopicRegistry::topics() const noexcept
{
    return topics_;
}

TopicRuntime* DdsTopicRegistry::find_topic(const std::string& rosbridge_topic) const noexcept
{
    const auto found = std::find_if(topics_.begin(), topics_.end(), [&](const auto& topic)
    {
        return topic->config.rosbridge_topic == rosbridge_topic;
    });
    return found == topics_.end() ? nullptr : found->get();
}

TopicRuntime* DdsTopicRegistry::find_type(const std::string& wire_type) const noexcept
{
    const auto found = std::find_if(topics_.begin(), topics_.end(), [&](const auto& topic)
    {
        return topic->config.wire_type == wire_type;
    });
    return found == topics_.end() ? nullptr : found->get();
}

std::vector<PublishedSample> DdsTopicRegistry::take_samples()
{
    std::vector<PublishedSample> samples;
    for (const auto& runtime : topics_)
    {
        SampleInfo info;
        auto data = DynamicDataFactory::get_instance()->create_data(runtime->dynamic_type);
        if (!data)
        {
            continue;
        }
        while (runtime->reader->take_next_sample(&data, &info) == RETCODE_OK)
        {
            if (!info.valid_data)
            {
                continue;
            }
            std::ostringstream json_stream;
            if (json_serialize(data, DynamicDataJsonFormat::OMG, json_stream) != RETCODE_OK)
            {
                continue;
            }
            eprosima::fastdds::rtps::SerializedPayload_t payload(
                    runtime->type_support->calculate_serialized_size(&data, XCDR_DATA_REPRESENTATION));
            std::vector<uint8_t> serialized;
            if (runtime->type_support->serialize(&data, payload, XCDR_DATA_REPRESENTATION))
            {
                serialized.assign(payload.data, payload.data + payload.length);
            }
            samples.push_back({runtime->config.rosbridge_topic, runtime->config.wire_type,
                    nlohmann::json::parse(json_stream.str()), std::move(serialized)});
        }
    }
    return samples;
}

void DdsTopicRegistry::write_json(TopicRuntime& topic, const nlohmann::json& message)
{
    DynamicData::_ref_type data;
    const auto result = json_deserialize(message.dump(), topic.dynamic_type, DynamicDataJsonFormat::OMG, data);
    if (result != RETCODE_OK || !data)
    {
        throw std::invalid_argument("message does not match configured IDL type '" + topic.config.type_name + "'");
    }
    if (topic.writer->write(&data) != RETCODE_OK)
    {
        throw std::runtime_error("Fast DDS could not write topic '" + topic.config.dds_topic + "'");
    }
}
