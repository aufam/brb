module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>

module brb;

template <typename F>
class defer {
public:
    defer(F fn)
        : fn(std::move(fn)) {}
    F fn;
    ~defer() {
        fn();
    }
};

auto brb::Router::handle(std::shared_ptr<tcp_stream> stream) const -> awaitable<bool> {
    Context ctx;
    ctx.stream = stream;

    auto &parser = ctx.parser_empty();
    co_await http::async_read_header(*stream, ctx.buffer, parser);

    const auto url = urls::parse_origin_form(parser.get().target());
    if (!url) {
        auto &res = ctx.response_empty();
        res.result(http::status::bad_request);
        co_await http::async_write(*stream, res);
        co_return std::visit([](auto &res) { return res.keep_alive(); }, ctx.r);
    }
    ctx.url = *url;

    {
        std::unique_lock<std::mutex> lock(_mtx);
        _tcp_streams.push_back(stream);
    };
    defer _ = [&]() {
        std::unique_lock<std::mutex> lock(_mtx);
        auto it = std::remove_if(_tcp_streams.begin(), _tcp_streams.end(), [&](auto &s) { return s == stream; });
        _tcp_streams.erase(it, _tcp_streams.end());
    };

    match(ctx);
    co_await ctx.next();
    co_return std::visit([](auto &res) { return res.keep_alive(); }, ctx.r);
}
