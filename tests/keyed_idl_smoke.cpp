#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicDataFactory.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicPubSubType.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicTypeBuilderFactory.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicTypeBuilder.hpp>
#include <fastdds/dds/xtypes/dynamic_types/MemberDescriptor.hpp>
#include <fastdds/dds/xtypes/utils.hpp>

#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace eprosima::fastdds::dds;

int main()
{
    auto type_factory = DynamicTypeBuilderFactory::get_instance();
    DynamicType::_ref_type vehicle_type;
    const auto result = type_factory->for_each_type_w_uri(VEHICLE_IDL_PATH, {},
            [&vehicle_type](DynamicTypeBuilder::_ref_type builder)
            {
                auto candidate = builder->build();
                if (candidate != nullptr && candidate->get_name() == "demo::msg::VehicleState")
                {
                    vehicle_type = candidate;
                }
                return true;
            });
    if (result != RETCODE_OK || vehicle_type == nullptr)
    {
        std::cerr << "Could not load demo::msg::VehicleState from " << VEHICLE_IDL_PATH << '\n';
        return 1;
    }

    DynamicTypeMember::_ref_type key_member;
    traits<MemberDescriptor>::ref_type key_descriptor = traits<MemberDescriptor>::make_shared();
    if (vehicle_type->get_member_by_name(key_member, "device_id") != RETCODE_OK ||
            key_member->get_descriptor(key_descriptor) != RETCODE_OK || !key_descriptor->is_key())
    {
        std::cerr << "VehicleState.device_id must be marked as a DDS key\n";
        return 1;
    }

    auto* participant_factory = DomainParticipantFactory::get_instance();
    auto* participant = participant_factory->create_participant(97, PARTICIPANT_QOS_DEFAULT);
    if (participant == nullptr)
    {
        std::cerr << "Could not create DDS test participant\n";
        return 1;
    }

    TypeSupport type_support(new DynamicPubSubType(vehicle_type));
    const auto type_name = std::string(vehicle_type->get_name().c_str());
    if (type_support.register_type(participant, type_name) != RETCODE_OK)
    {
        std::cerr << "Could not register keyed dynamic type\n";
        participant_factory->delete_participant(participant);
        return 1;
    }
    auto* topic = participant->create_topic("KeyedIdlSmoke", type_name, TOPIC_QOS_DEFAULT);
    auto* publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);
    auto* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
    DataReaderQos reader_qos = DATAREADER_QOS_DEFAULT;
    reader_qos.history().kind = KEEP_ALL_HISTORY_QOS;
    auto* writer = topic == nullptr || publisher == nullptr ? nullptr :
            publisher->create_datawriter(topic, DATAWRITER_QOS_DEFAULT);
    auto* reader = topic == nullptr || subscriber == nullptr ? nullptr :
            subscriber->create_datareader(topic, reader_qos);
    if (writer == nullptr || reader == nullptr)
    {
        std::cerr << "Could not create keyed DDS test endpoints\n";
        participant_factory->delete_participant(participant);
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const std::vector<std::string> messages = {
        R"({"device_id":101,"name":"alpha","mode":"demo::msg::MANUAL","position":{"x":1,"y":2,"z":3},"route":[],"covariance":[0,0,0]})",
        R"({"device_id":202,"name":"beta","mode":"demo::msg::AUTONOMOUS","position":{"x":4,"y":5,"z":6},"route":[],"covariance":[0,0,0]})",
        R"({"device_id":101,"name":"alpha updated","mode":"demo::msg::STOPPED","position":{"x":7,"y":8,"z":9},"route":[],"covariance":[0,0,0]})",
    };
    bool write_ok = true;
    for (const auto& message : messages)
    {
        DynamicData::_ref_type data;
        if (json_deserialize(message, vehicle_type, DynamicDataJsonFormat::OMG, data) != RETCODE_OK ||
                data == nullptr || writer->write(&data) != RETCODE_OK)
        {
            write_ok = false;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
    }

    std::vector<std::pair<int32_t, InstanceHandle_t>> received;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (write_ok && received.size() < messages.size() && std::chrono::steady_clock::now() < deadline)
    {
        DynamicData::_ref_type sample = DynamicDataFactory::get_instance()->create_data(vehicle_type);
        SampleInfo info;
        if (reader->take_next_sample(&sample, &info) == RETCODE_OK && info.valid_data)
        {
            int32_t device_id = 0;
            const auto member_id = sample->get_member_id_by_name("device_id");
            if (sample->get_int32_value(device_id, member_id) != RETCODE_OK)
            {
                write_ok = false;
                break;
            }
            received.emplace_back(device_id, info.instance_handle);
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::map<int32_t, std::vector<InstanceHandle_t>> handles_by_key;
    for (const auto& sample : received)
    {
        handles_by_key[sample.first].push_back(sample.second);
    }
    const bool same_key_same_instance = handles_by_key[101].size() == 2 &&
            handles_by_key[101][0] == handles_by_key[101][1];
    const bool different_key_different_instance = handles_by_key[101].size() == 2 &&
            handles_by_key[202].size() == 1 && handles_by_key[101][0] != handles_by_key[202][0];
    participant_factory->delete_participant(participant);
    if (!write_ok || received.size() != 3 || !same_key_same_instance || !different_key_different_instance)
    {
        std::cerr << "Expected device_id 101 writes to share a DDS instance and device_id 202 to use another; got "
                  << received.size() << " samples (write_ok=" << write_ok
                  << ", same=" << same_key_same_instance << ", distinct=" << different_key_different_instance << ")\n";
        return 1;
    }

    std::cout << "Keyed IDL smoke check passed: same key reuses an instance; distinct keys create distinct instances\n";
    return 0;
}
