#pragma once

#include "dds_topic.hpp"

#include <chrono>
#include <map>
#include <string>
#include <vector>

class TopicRateLimiter
{
public:
    using Clock = std::chrono::steady_clock;

    explicit TopicRateLimiter(const std::map<std::string, double>& rates_hz);

    std::vector<PublishedSample> process(
            std::vector<PublishedSample> samples,
            Clock::time_point now = Clock::now());

private:
    std::map<std::string, std::chrono::duration<double>> periods_;
    std::map<std::string, Clock::time_point> last_sent_;
    std::map<std::string, PublishedSample> pending_;
};
