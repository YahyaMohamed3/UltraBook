#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace ultraBook {

struct Tick {
    std::int64_t ts;   // epoch micro/nano as you prefer
    double       px;
    double       vol;
};

class IMarketDataFeed {
public:
    virtual ~IMarketDataFeed() = default;
    virtual bool load(const std::string& path) = 0;
    virtual const std::vector<Tick>& ticks() const = 0;
};

} // namespace ultraBook
