#include "topic_rate_limiter.hpp"

#include <stdexcept>
#include <utility>

TopicRateLimiter::TopicRateLimiter(const std::map<std::string, double>& rates_hz)
{
    for (const auto& entry : rates_hz)
    {
        if (entry.second <= 0.0)
        {
            throw std::invalid_argument("topic rate must be positive for " + entry.first);
        }
        periods_[entry.first] = std::chrono::duration<double>(1.0 / entry.second);
    }
}

std::vector<PublishedSample> TopicRateLimiter::process(
        std::vector<PublishedSample> samples,
        Clock::time_point now)
{
    std::vector<PublishedSample> output;
    for (auto& sample : samples)
    {
        if (periods_.find(sample.topic) == periods_.end())
        {
            output.push_back(std::move(sample));
        }
        else
        {
            pending_[sample.topic] = std::move(sample);
        }
    }

    for (auto pending = pending_.begin(); pending != pending_.end();)
    {
        const auto period = periods_.at(pending->first);
        const auto previous = last_sent_.find(pending->first);
        if (previous == last_sent_.end() || std::chrono::duration<double>(now - previous->second) >= period)
        {
            output.push_back(std::move(pending->second));
            last_sent_[pending->first] = now;
            pending = pending_.erase(pending);
        }
        else
        {
            ++pending;
        }
    }
    return output;
}
