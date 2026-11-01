#include <gtest/gtest.h>
#include <chrono>

import brb;

TEST(sse_dump, empty_returns_empty) {
    brb::SSE sse;
    EXPECT_EQ(sse.dump(), "");
}

TEST(sse_dump, only_id) {
    brb::SSE sse;
    sse.id = "1";
    EXPECT_EQ(sse.dump(), "");
}

TEST(sse_dump, only_event) {
    brb::SSE sse;
    sse.event = "message";
    EXPECT_EQ(sse.dump(), "event: message\n\n");
}

TEST(sse_dump, only_data) {
    brb::SSE sse;
    sse.data = "hello";
    EXPECT_EQ(sse.dump(), "data: hello\n\n");
}

TEST(sse_dump, only_retry) {
    brb::SSE sse;
    sse.retry = std::chrono::milliseconds(1000);
    EXPECT_EQ(sse.dump(), "");
}

TEST(sse_dump, all_fields) {
    brb::SSE sse;
    sse.id = "1";
    sse.event = "message";
    sse.data = "hello";
    sse.retry = std::chrono::milliseconds(1000);
    EXPECT_EQ(sse.dump(),
              "id: 1\n"
              "event: message\n"
              "data: hello\n"
              "retry: 1000\n\n");
}

TEST(sse_dump, data_with_trailing_newline) {
    brb::SSE sse;
    sse.data = "hello\n";
    EXPECT_EQ(sse.dump(), "data: hello\n\n\n");
}

TEST(sse_dump, data_multiple_lines) {
    brb::SSE sse;
    sse.data = "line1\nline2\n";
    EXPECT_EQ(sse.dump(),
              "data: line1\n"
              "line2\n\n\n");
}
