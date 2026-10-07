#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

import brb;

TEST(cors, cors) {
    const brb::middlewares::cors cors{
        .origins = {"https://example.com",          "https://foo.com"              },
        .methods = {brb::http::verb::get,           brb::http::verb::post          },
        .headers = {brb::http::field::content_type, brb::http::field::authorization},
    };

    // Origins
    EXPECT_TRUE(cors.allowed_origin("https://example.com"));
    EXPECT_TRUE(cors.allowed_origin("https://foo.com"));
    EXPECT_FALSE(cors.allowed_origin("https://evil.com"));

    // Methods
    EXPECT_TRUE(cors.allowed_method(brb::http::verb::get));
    EXPECT_TRUE(cors.allowed_method(brb::http::verb::post));
    EXPECT_FALSE(cors.allowed_method(brb::http::verb::delete_));

    // Headers
    EXPECT_TRUE(cors.allowed_header("Content-Type"));
    EXPECT_TRUE(cors.allowed_header("Authorization"));
    EXPECT_FALSE(cors.allowed_header("X-Custom-Header"));
    EXPECT_FALSE(cors.allowed_header("Definitely-Not-A-Header"));
}

TEST(cors, wildcard_origin) {
    const brb::middlewares::cors cors{
        .origins = {"*"},
        .methods = {brb::http::verb::get},
        .headers = {},
    };

    EXPECT_TRUE(cors.allowed_origin("https://example.com"));
    EXPECT_TRUE(cors.allowed_origin("https://evil.com"));
    EXPECT_TRUE(cors.allowed_origin(""));
}

TEST(cors, middleware) {
    brb::Router r;

    const auto no_response_writer = [](brb::Context &c) -> brb::awaitable<void> {
        c.res().result(brb::http::status::not_found);
        std::ignore = c.response_buffer(); // convert to buffer body response, just to avoid response write
        co_await c.next();
    };

    const auto cors = brb::middlewares::cors{
        .origins = {"https://example.com"},
        .methods = {brb::http::verb::get, brb::http::verb::post},
        .headers = {},
    };

    r.use(no_response_writer, cors);

    r.route(brb::http::verb::get, "/users", [](brb::Context &c) -> brb::awaitable<void> {
        c.res().result(brb::http::status::accepted);
        co_return;
    });

    // Normal request with Origin
    {
        brb::Context c;
        c.url_path = "/users";

        auto &req = const_cast<brb::request_header &>(c.req());
        req.method(brb::http::verb::get);
        req.set(brb::http::field::origin, "https://example.com");

        c.match(r);

        brb::asio::io_context io;
        brb::asio::co_spawn(io, c.next(), brb::asio::detached);
        io.run();

        EXPECT_EQ(c.res().result(), brb::http::status::accepted);
        EXPECT_EQ(c.res()[brb::http::field::access_control_allow_origin], "https://example.com");
        EXPECT_EQ(c.res()[brb::http::field::access_control_allow_methods], "");
    }

    // Preflight request
    {
        brb::Context c;
        c.url_path = "/users";

        auto &req = const_cast<brb::request_header &>(c.req());
        req.method(brb::http::verb::options);
        req.set(brb::http::field::origin, "https://example.com");
        req.set(brb::http::field::access_control_request_method, "GET");

        c.match(r);

        brb::asio::io_context io;
        brb::asio::co_spawn(io, c.next(), brb::asio::detached);
        io.run();

        EXPECT_EQ(c.res().result(), brb::http::status::no_content);
        EXPECT_EQ(c.res()[brb::http::field::access_control_allow_origin], "https://example.com");
        EXPECT_EQ(c.res()[brb::http::field::access_control_allow_methods], "GET, POST");
    }

    // Preflight request rejected
    {
        brb::Context c;
        c.url_path = "/users";

        auto &req = const_cast<brb::request_header &>(c.req());
        req.method(brb::http::verb::options);
        req.set(brb::http::field::origin, "https://example.com");
        req.set(brb::http::field::access_control_request_method, "DELETE");

        c.match(r);

        brb::asio::io_context io;
        brb::asio::co_spawn(io, c.next(), brb::asio::detached);
        io.run();

        EXPECT_EQ(c.res().result(), brb::http::status::forbidden);
        EXPECT_EQ(c.res()[brb::http::field::access_control_allow_origin], "https://example.com");
        EXPECT_EQ(c.res()[brb::http::field::access_control_allow_methods], "");
    }

    // Request without Origin
    {
        brb::Context c;
        c.url_path = "/users";

        auto &req = const_cast<brb::request_header &>(c.req());
        req.method(brb::http::verb::get);

        c.match(r);

        brb::asio::io_context io;
        brb::asio::co_spawn(io, c.next(), brb::asio::detached);
        io.run();

        EXPECT_EQ(c.res().result(), brb::http::status::accepted);
        EXPECT_EQ(c.res()[brb::http::field::access_control_allow_origin], "");
        EXPECT_EQ(c.res()[brb::http::field::access_control_allow_methods], "");
    }
}
