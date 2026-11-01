#include <gtest/gtest.h>
#include <boost/beast.hpp>

import brb;

TEST(part_dump, empty_part) {
    brb::Part part;
    EXPECT_EQ(part.dump("boundary"), "--boundary\r\n\r\n\r\n");
}

TEST(part_dump, with_fields_and_body) {
    brb::Part part;
    part.fields.set(brb::http::field::content_type, "text/plain");
    part.body = "hello";
    EXPECT_EQ(part.dump("boundary"),
              "--boundary\r\n"
              "Content-Type: text/plain\r\n"
              "\r\n"
              "hello\r\n");
}

TEST(part_dump, with_body_only) {
    brb::Part part;
    part.body = "world";
    EXPECT_EQ(part.dump("boundary"),
              "--boundary\r\n"
              "\r\n"
              "world\r\n");
}

TEST(part_dump, multiple_fields) {
    brb::Part part;
    part.fields.set(brb::http::field::content_type, "text/plain");
    part.fields.set(brb::http::field::content_length, "5");
    part.body = "hello";
    EXPECT_EQ(part.dump("boundary"),
              "--boundary\r\n"
              "Content-Type: text/plain\r\n"
              "Content-Length: 5\r\n"
              "\r\n"
              "hello\r\n");
}

TEST(part_dump_end, returns_final_boundary) {
    EXPECT_EQ(brb::Part::dump_end("boundary"), "--boundary--\r\n");
}
