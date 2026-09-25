module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

module brb;

auto brb::middlewares::string_body(Context &c) -> awaitable<void> {
    auto &parser = c.parser_string();
    auto &res    = c.response_string();

    if (!parser.is_done())
        co_await http::async_read(*c.stream, c.buffer, parser);

    co_await c.next();

    res.prepare_payload();
    co_await http::async_write(*c.stream, res);
}
