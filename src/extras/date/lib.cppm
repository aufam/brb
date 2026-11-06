module;

#include <chrono>
#include <string>

export module brb.date;

export namespace brb::date {
    using time_point = std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>;

    time_point  parse(std::string_view value);
    std::string format(time_point tp);

    std::string format(std::chrono::system_clock::time_point tp) {
        return format(std::chrono::time_point_cast<std::chrono::seconds>(tp));
    }
} // namespace brb::date
