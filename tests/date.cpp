#include <gtest/gtest.h>

import brb.date;

TEST(date_parse, valid_rfc7231_example) {
    const auto tp = brb::date::parse("Sun, 06 Nov 1994 08:49:37 GMT");
    EXPECT_EQ(brb::date::format(tp), "Sun, 06 Nov 1994 08:49:37 GMT");
}

TEST(date_parse, round_trip) {
    const std::vector<std::string> dates = {
        "Mon, 02 Jan 2006 15:04:05 GMT",
        "Tue, 03 Jan 2006 15:04:05 GMT",
        "Wed, 04 Jan 2006 15:04:05 GMT",
        "Thu, 05 Jan 2006 15:04:05 GMT",
        "Fri, 06 Jan 2006 15:04:05 GMT",
        "Sat, 07 Jan 2006 15:04:05 GMT",
        "Sun, 08 Jan 2006 15:04:05 GMT",
        "Wed, 29 Feb 2012 00:00:00 GMT",
        "Fri, 31 Dec 9999 23:59:59 GMT",
    };

    for (const auto &d : dates) {
        EXPECT_EQ(brb::date::format(brb::date::parse(d)), d);
    }
}

TEST(date_parse, epoch) {
    const auto tp = std::chrono::system_clock::time_point{};
    const auto s  = brb::date::format(tp);
    EXPECT_EQ(brb::date::parse(s), tp);
}

TEST(date_parse, empty_throws) {
    EXPECT_THROW(brb::date::parse(""), std::runtime_error);
}

TEST(date_parse, too_short_throws) {
    EXPECT_THROW(brb::date::parse("Sun, 06 Nov 1994"), std::runtime_error);
}

TEST(date_parse, missing_comma_throws) {
    EXPECT_THROW(brb::date::parse("Sun 06 Nov 1994 08:49:37 GMT"), std::runtime_error);
}

TEST(date_parse, missing_gmt_throws) {
    EXPECT_THROW(brb::date::parse("Sun, 06 Nov 1994 08:49:37 UT"), std::runtime_error);
}

TEST(date_parse, invalid_weekday_throws) {
    EXPECT_THROW(brb::date::parse("Foo, 06 Nov 1994 08:49:37 GMT"), std::runtime_error);
}

TEST(date_parse, invalid_month_throws) {
    EXPECT_THROW(brb::date::parse("Sun, 06 Xxx 1994 08:49:37 GMT"), std::runtime_error);
}

TEST(date_parse, day_out_of_range_throws) {
    EXPECT_THROW(brb::date::parse("Sun, 00 Nov 1994 08:49:37 GMT"), std::runtime_error);
    EXPECT_THROW(brb::date::parse("Sun, 32 Nov 1994 08:49:37 GMT"), std::runtime_error);
}

TEST(date_parse, year_out_of_range_throws) {
    EXPECT_THROW(brb::date::parse("Sun, 06 Nov 1600 08:49:37 GMT"), std::runtime_error);
}

TEST(date_parse, hour_out_of_range_throws) {
    EXPECT_THROW(brb::date::parse("Sun, 06 Nov 1994 24:49:37 GMT"), std::runtime_error);
}

TEST(date_parse, minute_out_of_range_throws) {
    EXPECT_THROW(brb::date::parse("Sun, 06 Nov 1994 08:60:37 GMT"), std::runtime_error);
}

TEST(date_parse, second_out_of_range_throws) {
    EXPECT_THROW(brb::date::parse("Sun, 06 Nov 1994 08:49:61 GMT"), std::runtime_error);
}

TEST(date_parse, invalid_day_month_combination_throws) {
    EXPECT_THROW(brb::date::parse("Sun, 31 Feb 1994 08:49:37 GMT"), std::runtime_error);
    EXPECT_THROW(brb::date::parse("Sun, 30 Feb 1994 08:49:37 GMT"), std::runtime_error);
}

TEST(date_parse, non_digit_day_throws) {
    EXPECT_THROW(brb::date::parse("Sun, ab Nov 1994 08:49:37 GMT"), std::runtime_error);
}

TEST(date_format, epoch_string) {
    const auto tp = std::chrono::system_clock::time_point{};
    EXPECT_EQ(brb::date::format(tp), "Thu, 01 Jan 1970 00:00:00 GMT");
}

TEST(date_format, leap_year_feb_29) {
    const auto tp = brb::date::parse("Wed, 29 Feb 2012 00:00:00 GMT");
    EXPECT_EQ(brb::date::format(tp), "Wed, 29 Feb 2012 00:00:00 GMT");
}

TEST(date_format, end_of_century) {
    const auto tp = brb::date::parse("Fri, 31 Dec 1999 23:59:59 GMT");
    EXPECT_EQ(brb::date::format(tp), "Fri, 31 Dec 1999 23:59:59 GMT");
}
