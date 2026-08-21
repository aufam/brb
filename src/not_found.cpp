module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

module brb;

auto brb::Router::not_found(Context &c) const -> awaitable<void> {
    auto &res = c.response_string();
    res.result(http::status::not_found);
    res.set(http::field::content_type, "text/plain");
    res.body() = "404 Not Found";
    res.prepare_payload();
    co_await http::async_write(*c.stream, res);
}
