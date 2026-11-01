#include <gtest/gtest.h>
#include <boost/beast.hpp>

import brb;

TEST(accepts, empty_header_returns_false) {
    brb::Context c;
    EXPECT_FALSE(c.accepts("text/html"));
}

TEST(accepts, exact_match) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept, "text/html");
    EXPECT_TRUE(c.accepts("text/html"));
    EXPECT_FALSE(c.accepts("application/json"));
}

TEST(accepts, case_insensitive) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept, "TEXT/HTML");
    EXPECT_TRUE(c.accepts("text/html"));
}

TEST(accepts, type_wildcard_matches_subtype) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept, "application/*");
    EXPECT_TRUE(c.accepts("application/json"));
    EXPECT_TRUE(c.accepts("application/xml"));
    EXPECT_FALSE(c.accepts("text/html"));
}

TEST(accepts, star_star_matches_everything) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept, "*/*");
    EXPECT_FALSE(c.accepts("text/html"));
    EXPECT_FALSE(c.accepts("application/json"));
}

TEST(accepts, q_zero_does_not_match) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept, "text/html;q=0");
    EXPECT_FALSE(c.accepts("text/html"));
}

TEST(accepts, q_zero_with_spaces) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept, "text/html ; q=0");
    EXPECT_FALSE(c.accepts("text/html"));
}

TEST(accepts, multiple_values_one_matches) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept, "text/plain, text/html");
    EXPECT_TRUE(c.accepts("text/html"));
    EXPECT_FALSE(c.accepts("application/json"));
}

TEST(accepts, q_zero_with_other_values) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept, "text/html;q=0, application/json");
    EXPECT_FALSE(c.accepts("text/html"));
    EXPECT_TRUE(c.accepts("application/json"));
}

TEST(accepts_encoding, empty_header_returns_false) {
    brb::Context c;
    EXPECT_FALSE(c.accepts_encoding("gzip"));
}

TEST(accepts_encoding, exact_match) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept_encoding, "gzip");
    EXPECT_TRUE(c.accepts_encoding("gzip"));
    EXPECT_FALSE(c.accepts_encoding("br"));
}

TEST(accepts_encoding, case_insensitive) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept_encoding, "GZIP");
    EXPECT_TRUE(c.accepts_encoding("gzip"));
}

TEST(accepts_encoding, star_matches_everything) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept_encoding, "*");
    EXPECT_TRUE(c.accepts_encoding("gzip"));
    EXPECT_TRUE(c.accepts_encoding("br"));
    EXPECT_TRUE(c.accepts_encoding("zstd"));
}

TEST(accepts_encoding, multiple_values) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept_encoding, "gzip, deflate");
    EXPECT_TRUE(c.accepts_encoding("gzip"));
    EXPECT_TRUE(c.accepts_encoding("deflate"));
    EXPECT_FALSE(c.accepts_encoding("br"));
}

TEST(accepts_encoding, no_match) {
    brb::Context c;
    auto &req = const_cast<brb::request_header &>(c.req());
    req.set(brb::http::field::accept_encoding, "gzip");
    EXPECT_FALSE(c.accepts_encoding("br"));
}
