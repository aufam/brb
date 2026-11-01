#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

import brb;

TEST(router, literal_match) {
    brb::Router r;

    int n = 0;

    r.route(brb::http::verb::unknown, "/users", [&](brb::Context &) -> brb::awaitable<void> {
        n = 1;
        co_return;
    });

    brb::Context c;
    c.url_path = "/users";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 1);
}

TEST(router, no_match) {
    brb::Router r;

    int n = 0;

    r.route(brb::http::verb::unknown, "/users", [&](brb::Context &) -> brb::awaitable<void> {
        n = 1;
        co_return;
    });

    brb::Context c;
    c.url_path = "/posts";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 0);
}

TEST(router, nested_literal) {
    brb::Router r;

    int n = 0;

    r.route(brb::http::verb::unknown, "/users/profile", [&](brb::Context &) -> brb::awaitable<void> {
        n = 1;
        co_return;
    });

    brb::Context c;
    c.url_path = "/users/profile";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 1);
}

TEST(router, literal_prefix_no_match) {
    brb::Router r;

    int n = 0;

    r.route(brb::http::verb::unknown, "/users", [&](brb::Context &) -> brb::awaitable<void> {
        n = 1;
        co_return;
    });

    brb::Context c;
    c.url_path = "/users/123";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 0);
}

TEST(router, parameter) {
    brb::Router r;

    int n = 0;

    r.route(brb::http::verb::unknown, "/users/:id", [&](brb::Context &c) -> brb::awaitable<void> {
        n = 1;

        EXPECT_EQ(c.get<std::string>(":id"), "123");

        co_return;
    });

    brb::Context c;
    c.url_path = "/users/123";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 1);
}

TEST(router, literal_precedes_parameter) {
    brb::Router r;

    int n = 0;

    r.route(brb::http::verb::unknown, "/users/me", [&](brb::Context &) -> brb::awaitable<void> {
        n = 1;
        co_return;
    });

    r.route(brb::http::verb::unknown, "/users/:id", [&](brb::Context &) -> brb::awaitable<void> {
        n = 2;
        co_return;
    });

    brb::Context c;
    c.url_path = "/users/me";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 1);
}

TEST(router, parameter_fallback) {
    brb::Router r;

    int n = 0;

    r.route(brb::http::verb::unknown, "/users/me", [&](brb::Context &) -> brb::awaitable<void> {
        n = 1;
        co_return;
    });

    r.route(brb::http::verb::unknown, "/users/:id", [&](brb::Context &c) -> brb::awaitable<void> {
        n = 2;
        EXPECT_EQ(c.get<std::string>(":id"), "123");
        co_return;
    });

    brb::Context c;
    c.url_path = "/users/123";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 2);
}

TEST(router, multiple_parameters) {
    brb::Router r;

    r.route(brb::http::verb::unknown, "/users/:user_id/posts/:post_id", [&](brb::Context &c) -> brb::awaitable<void> {
        EXPECT_EQ(c.get<std::string>(":user_id"), "42");
        EXPECT_EQ(c.get<std::string>(":post_id"), "100");
        co_return;
    });

    brb::Context c;
    c.url_path = "/users/42/posts/100";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();
}

TEST(router, method_match) {
    brb::Router r;

    int n = 0;

    r.route(brb::http::verb::get, "/users", [&](brb::Context &) -> brb::awaitable<void> {
        n = 1;
        co_return;
    });

    r.route(brb::http::verb::post, "/users", [&](brb::Context &) -> brb::awaitable<void> {
        n = 2;
        co_return;
    });

    brb::Context c;
    c.url_path = "/users";

    const_cast<brb::request_header &>(c.req()).method(brb::http::verb::post);

    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 2);
}

TEST(router, trailing_slash) {
    brb::Router r;

    int n = 0;

    r.route(brb::http::verb::unknown, "/users", [&](brb::Context &) -> brb::awaitable<void> {
        n = 1;
        co_return;
    });

    brb::Context c;
    c.url_path = "/users/";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 0);
}

TEST(router, root) {
    brb::Router r;

    int n = 0;

    r.route(brb::http::verb::unknown, "/", [&](brb::Context &) -> brb::awaitable<void> {
        n = 1;
        co_return;
    });

    brb::Context c;
    c.url_path = "/";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 1);
}

TEST(router, middleware) {
    brb::Router r;

    std::vector<int> calls;

    r.use([&](brb::Context &c) -> brb::awaitable<void> {
        calls.push_back(1);
        co_await c.next();
    });

    r.route(brb::http::verb::unknown, "/users", [&](brb::Context &) -> brb::awaitable<void> {
        calls.push_back(2);
        co_return;
    });

    brb::Context c;
    c.url_path = "/users";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(calls, (std::vector<int>{1, 2}));
}

TEST(router, middleware_no_next) {
    brb::Router r;

    std::vector<int> calls;

    r.use([&](brb::Context &) -> brb::awaitable<void> {
        calls.push_back(1);
        co_return;
    });

    r.route(brb::http::verb::unknown, "/users", [&](brb::Context &) -> brb::awaitable<void> {
        calls.push_back(2);
        co_return;
    });

    brb::Context c;
    c.url_path = "/users";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(calls, std::vector<int>{1});
}

TEST(router, middleware_called_on_no_match) {
    brb::Router r;

    int n = 0;

    r.use([&](brb::Context &c) -> brb::awaitable<void> {
        n = 1;
        co_await c.next();
    });

    r.route(brb::http::verb::unknown, "/users", [&](brb::Context &) -> brb::awaitable<void> {
        n = 2;
        co_return;
    });

    brb::Context c;
    c.url_path = "/unknown";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(n, 1);
}

TEST(router, nested_middleware) {
    brb::Router r;

    std::vector<int> calls;

    r.use([&](brb::Context &c) -> brb::awaitable<void> {
        calls.push_back(1);
        co_await c.next();
    });

    auto &users = r.group("/users");

    users.use([&](brb::Context &c) -> brb::awaitable<void> {
        calls.push_back(2);
        co_await c.next();
    });

    users.route(brb::http::verb::unknown, "/:id", [&](brb::Context &) -> brb::awaitable<void> {
        calls.push_back(3);
        co_return;
    });

    brb::Context c;
    c.url_path = "/users/123";
    c.match(r);

    brb::asio::io_context io;
    brb::asio::co_spawn(io, c.next(), brb::asio::detached);
    io.run();

    EXPECT_EQ(calls, (std::vector<int>{1, 2, 3}));
}
