#include <gtest/gtest.h>
#include <boost/beast.hpp>

import brb;

TEST(encoded, empty_header_returns_false) {
    brb::Context c;
    EXPECT_FALSE(c.is_encoded("gzip"));
}

TEST(encoded, exact_match) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::content_encoding, "gzip");
    EXPECT_TRUE(c.is_encoded("gzip"));
    EXPECT_FALSE(c.is_encoded("br"));
}

TEST(encoded, case_insensitive) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::content_encoding, "GZIP");
    EXPECT_TRUE(c.is_encoded("gzip"));
}

TEST(encoded, multiple_values_one_matches) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::content_encoding, "gzip, br");
    EXPECT_TRUE(c.is_encoded("gzip"));
    EXPECT_TRUE(c.is_encoded("br"));
    EXPECT_FALSE(c.is_encoded("zstd"));
}

TEST(encoded, multiple_values_with_spaces) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::content_encoding, "gzip, br, zstd");
    EXPECT_TRUE(c.is_encoded("gzip"));
    EXPECT_TRUE(c.is_encoded("br"));
    EXPECT_TRUE(c.is_encoded("zstd"));
    EXPECT_FALSE(c.is_encoded("deflate"));
}

TEST(encoded, no_match) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::content_encoding, "gzip");
    EXPECT_FALSE(c.is_encoded("br"));
}
