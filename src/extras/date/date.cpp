module;

#include <chrono>
#include <string>

module brb.date;

brb::date::time_point brb::date::parse(std::string_view value) {
    // IMF-fixdate is exactly:
    //   wkday, SP DD SP month SP YYYY SP HH ":" MM ":" SS SP GMT
    if (value.size() != 29 || value[3] != ',' || value[4] != ' ' || value[7] != ' ' || value[11] != ' ' || value[16] != ' ' ||
        value[19] != ':' || value[22] != ':' || value[25] != ' ' || value.substr(26) != "GMT") {
        throw std::runtime_error("invalid format");
    }

    constexpr std::array<std::string_view, 12> months = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    auto digit = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : -1; };

    auto number = [&](std::size_t pos, std::size_t count) -> int {
        int result = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto d = digit(value[pos + i]);
            if (d < 0)
                return -1;
            result = result * 10 + d;
        }
        return result;
    };

    // Validate weekday.
    constexpr std::array<std::string_view, 7> weekdays = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    bool valid_weekday = false;
    for (auto weekday : weekdays) {
        if (value.substr(0, 3) == weekday) {
            valid_weekday = true;
            break;
        }
    }

    if (!valid_weekday)
        throw std::runtime_error("invalid weekday");

    const int day    = number(5, 2);
    const int year   = number(12, 4);
    const int hour   = number(17, 2);
    const int minute = number(20, 2);
    const int second = number(23, 2);

    if (day < 1 || day > 31 || year < 1601 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
        throw std::runtime_error("invalid number");
    }

    int month = 0;
    for (std::size_t i = 0; i < months.size(); ++i) {
        if (value.substr(8, 3) == months[i]) {
            month = static_cast<int>(i) + 1;
            break;
        }
    }

    if (month == 0)
        throw std::runtime_error("invalid month");

    // Howard Hinnant's civil-date -> days algorithm.
    // Produces days relative to 1970-01-01 without involving the
    // local timezone.
    const auto days_from_civil = [](int y, unsigned m, unsigned d) {
        y -= m <= 2;
        const int      era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

        return era * 146097 + static_cast<int>(doe) - 719468;
    };

    // Howard Hinnant's civil_from_days algorithm (inverse of days_from_civil).
    auto civil_from_days = [](std::int64_t z) {
        z += 719468;

        const auto era = (z >= 0 ? z : z - 146096) / 146097;
        const auto doe = static_cast<unsigned>(z - era * 146097);
        const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;

        auto       y   = static_cast<std::int64_t>(yoe) + era * 400;
        const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);

        const auto mp = (5 * doy + 2) / 153;
        const auto d  = doy - (153 * mp + 2) / 5 + 1;
        const auto m  = mp + (mp < 10 ? 3 : -9);

        y += m <= 2;

        return std::array<std::int64_t, 3>{y, m, d};
    };

    // Basic calendar validation.
    const int days = days_from_civil(year, month, day);

    // Check that converting the date didn't normalize an invalid
    // day/month combination.
    if (civil_from_days(days)[0] != year || civil_from_days(days)[1] != month ||
        civil_from_days(days)[2] != day)
        throw std::runtime_error("invalid day");

    return std::chrono::system_clock::time_point{
        std::chrono::seconds{static_cast<std::int64_t>(days) * 86400 + hour * 3600 + minute * 60 + second}
    };
}

std::string brb::date::format(std::chrono::system_clock::time_point tp) {
    const auto time     = std::chrono::time_point_cast<std::chrono::seconds>(tp);
    const auto duration = time.time_since_epoch();
    const auto seconds  = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

    // Convert Unix timestamp to civil date.
    const auto days        = seconds / 86400;
    const auto day_seconds = seconds % 86400;

    // Howard Hinnant's civil_from_days algorithm.
    auto civil_from_days = [](std::int64_t z) {
        z += 719468;

        const auto era = (z >= 0 ? z : z - 146096) / 146097;
        const auto doe = static_cast<unsigned>(z - era * 146097);
        const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;

        auto       y   = static_cast<std::int64_t>(yoe) + era * 400;
        const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);

        const auto mp = (5 * doy + 2) / 153;
        const auto d  = doy - (153 * mp + 2) / 5 + 1;
        const auto m  = mp + (mp < 10 ? 3 : -9);

        y += m <= 2;

        return std::array<std::int64_t, 3>{y, m, d};
    };

    const auto date = civil_from_days(days);

    // days since 1970-01-01; 1970-01-01 was Thursday.
    const auto weekday = static_cast<unsigned>((days % 7 + 7) % 7);

    constexpr std::array<std::string_view, 7> weekdays = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};

    constexpr std::array<std::string_view, 12> months = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    const auto hour   = static_cast<unsigned>(day_seconds / 3600);
    const auto minute = static_cast<unsigned>((day_seconds % 3600) / 60);
    const auto second = static_cast<unsigned>(day_seconds % 60);

    std::string result;
    result.reserve(29);

    result += weekdays[weekday];
    result += ", ";

    auto append_2 = [&](unsigned value) {
        result += static_cast<char>('0' + value / 10);
        result += static_cast<char>('0' + value % 10);
    };

    auto append_4 = [&](std::int64_t value) {
        result += static_cast<char>('0' + (value / 1000) % 10);
        result += static_cast<char>('0' + (value / 100) % 10);
        result += static_cast<char>('0' + (value / 10) % 10);
        result += static_cast<char>('0' + value % 10);
    };

    append_2(static_cast<unsigned>(date[2]));
    result += ' ';
    result += months[static_cast<unsigned>(date[1]) - 1];
    result += ' ';
    append_4(date[0]);
    result += ' ';
    append_2(hour);
    result += ':';
    append_2(minute);
    result += ':';
    append_2(second);
    result += " GMT";

    return result;
}
