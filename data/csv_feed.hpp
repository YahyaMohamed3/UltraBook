#pragma once
#include "feed.hpp"
#include <fstream>
#include <sstream>

namespace ultraBook {

class CSVFeed : public IMarketDataFeed {
public:
    bool load(const std::string& path) override;
    const std::vector<Tick>& ticks() const override { return data_; }

private:
    std::vector<Tick> data_;
};

} // namespace ultraBook
