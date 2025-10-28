#include "csv_feed.hpp"
#include <iostream>

namespace ultraBook {

bool CSVFeed::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "CSVFeed: cannot open " << path << "\n";
        return false;
    }
    data_.clear();
    std::string line;
    // optional header
    std::getline(f, line);
    // Heuristically treat first line as header if it has alpha
    auto has_alpha = [](const std::string& s){
        for (char c: s) if (std::isalpha((unsigned char)c)) return true;
        return false;
    };
    if (!f || line.empty() || !has_alpha(line)) {
        // first line is data; parse it and continue
        if (!line.empty()) {
            std::istringstream ss(line);
            std::string t,p,v;
            if (std::getline(ss,t,',') && std::getline(ss,p,',') && std::getline(ss,v,',')) {
                data_.push_back({ std::stoll(t), std::stod(p), std::stod(v) });
            }
        }
    }

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string t,p,v;
        if (std::getline(ss,t,',') && std::getline(ss,p,',') && std::getline(ss,v,',')) {
            data_.push_back({ std::stoll(t), std::stod(p), std::stod(v) });
        }
    }
    return !data_.empty();
}

} // namespace ultraBook
