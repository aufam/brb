module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>
#include <filesystem>

module brb;
namespace fs = std::filesystem;

static auto trim(std::string_view value) -> std::string_view {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);

    return value;
}

static auto split_header_list(std::string_view value) -> std::vector<std::string_view> {
    std::vector<std::string_view> result;

    while (!value.empty()) {
        auto pos = value.find(',');

        auto item = trim(value.substr(0, pos));

        if (!item.empty())
            result.push_back(item);

        if (pos == std::string_view::npos)
            break;

        value.remove_prefix(pos + 1);
    }

    return result;
}

auto brb::middlewares::request_reader::operator()(Context &c) const -> awaitable<void> {
    if (string_body) {
        auto &parser = c.parser_string();
        if (!parser.is_done())
            co_await http::async_read(*c.stream, c.buffer, parser);

        co_await c.next();
    }

    else if (file_body) {
        fs::path dir = fs::temp_directory_path();
        auto     id  = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path tmp = dir / ("brb_temp_" + std::to_string(id) + ".tmp");
        defer    _   = [&]() { fs::remove(tmp); };

        const auto tmp_str = tmp.string();

        auto &parser = c.parser_file(tmp_str);
        if (!parser.is_done())
            co_await http::async_read(*c.stream, c.buffer, parser);

        co_await c.next();
    }
}

auto brb::middlewares::response_writer::operator()(Context &c) const -> awaitable<void> {
    co_await c.next();
    co_await c.write_response();
}

auto brb::middlewares::require_method::operator()(Context &c) const -> awaitable<void> {
    if (c.req().method() == method) {
        co_await c.next();
        co_return;
    }

    c.res().result(http::status::method_not_allowed);
    if (on_not_allowed) {
        co_await on_not_allowed(c);
    }

    co_await c.write_response();
}

auto brb::middlewares::require_content_type::operator()(Context &c) const -> awaitable<void> {
    auto ct = c.req()[http::field::content_type];
    bool ok = false;
    for (auto &content_type : content_types) {
        if (ct.starts_with(content_type)) {
            ok = true;
            break;
        }
    }

    if (ok) {
        co_await c.next();
        co_return;
    }

    c.res().result(http::status::unsupported_media_type);

    if (on_unsupported) {
        co_await on_unsupported(c);
    }

    co_await c.write_response();
}

auto brb::middlewares::timeout::operator()(Context &c) const -> awaitable<void> {
    auto io = co_await boost::asio::this_coro::executor;

    asio::steady_timer timer(io);
    timer.expires_after(duration);

    asio::co_spawn(
        io,
        [&]() -> awaitable<void> {
            boost::system::error_code ec;
            co_await timer.async_wait(asio::redirect_error(ec));
            if (ec)
                co_return;

            c.keep_alive(false);
            c.stream->close();
        }(),
        asio::detached
    );

    co_await c.next();
    timer.cancel();
}

auto brb::middlewares::cors::operator()(Context &c) const -> awaitable<void> {
    auto &req = c.req();
    auto &res = c.res();

    auto origin = req[http::field::origin];

    // Not a CORS request.
    if (origin.empty()) {
        co_await c.next();
        co_return;
    }

    if (!allowed_origin(origin)) {
        co_await c.next();
        co_return;
    }

    if (!credentials && std::find(origins.begin(), origins.end(), "*") != origins.end())
        res.set(http::field::access_control_allow_origin, "*");
    else
        res.set(http::field::access_control_allow_origin, origin);
    c.append_vary(http::field::origin);

    if (credentials)
        res.set(http::field::access_control_allow_credentials, "true");

    const auto requested_method  = http::string_to_verb(req[http::field::access_control_request_method]);
    const auto requested_headers = req[http::field::access_control_request_headers];

    // Preflight
    if (req.method() == http::verb::options && requested_method != http::verb::unknown) {
        if (!allowed_method(requested_method)) {
            res.result(http::status::forbidden);
            co_await c.write_response();
            co_return;
        }

        for (std::string_view header : split_header_list(requested_headers)) {
            if (!allowed_header(header)) {
                res.result(http::status::forbidden);
                co_await c.write_response();
                co_return;
            }
        }

        if (!methods.empty()) {
            std::string value;

            for (auto method : methods) {
                if (!value.empty())
                    value += ", ";

                value += http::to_string(method);
            }

            res.set(http::field::access_control_allow_methods, value);
        }

        if (!headers.empty()) {
            std::string value;

            for (auto header : headers) {
                if (!value.empty())
                    value += ", ";

                value += http::to_string(header);
            }

            res.set(http::field::access_control_allow_headers, value);
        }

        if (max_age.count() > 0) {
            res.set(http::field::access_control_max_age, std::to_string(max_age.count()));
        }

        res.result(http::status::no_content);
        co_await c.write_response();
        co_return;
    }

    co_await c.next();
}
