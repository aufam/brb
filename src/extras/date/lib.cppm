module;

#include <chrono>
#include <string>

export module brb.date;

export namespace brb::date {
    using time_point = std::chrono::system_clock::time_point;

    time_point  parse(std::string_view value);
    std::string format(time_point tp);
} // namespace brb::date
