module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>

module brb;

static std::string get_multipart_boundary(std::string_view);

auto brb::Engine::handle(std::shared_ptr<tcp_stream> stream, boost::optional<uint64_t> body_limit) const -> awaitable<bool> {
    Context ctx;
    ctx.stream   = stream;
    auto &parser = ctx.parser_empty();
    auto &req    = parser.get().base();

    parser.body_limit(body_limit);
    try {
        co_await http::async_read_header(*stream, ctx.buffer, parser);
    } catch (boost::system::system_error &e) {
        std::ignore = e;
    }

    ctx.multipart_boundary = get_multipart_boundary(req[brb::http::field::content_type]);

    auto &res = ctx.response_empty();
    res.keep_alive(parser.get().keep_alive());

    const auto url = urls::parse_origin_form(req.target());
    if (!url) {
        res.result(http::status::bad_request);
        try {
            co_await http::async_write(*stream, res);
        } catch (boost::system::system_error &e) {
            std::ignore = e;
        }
        co_return ctx.keep_alive();
    }
    ctx.url   = *url;
    auto path = ctx.url.path();

    ctx.match(*this, path);

    {
        std::unique_lock<std::mutex> lock(mtx);
        streams.push_back(stream);
    };
    defer _ = [&]() {
        std::unique_lock<std::mutex> lock(mtx);
        auto                         it = std::remove_if(streams.begin(), streams.end(), [&](auto &s) { return s == stream; });
        streams.erase(it, streams.end());
    };

    try {
        co_await ctx.next();
        co_await ctx.write_response();
    } catch (boost::system::system_error &e) {
        std::ignore = e;
    }

    co_return ctx.keep_alive();
}

std::string get_multipart_boundary(std::string_view content_type) {
    const auto value = content_type;

    constexpr std::string_view prefix = "multipart/";

    // Must be multipart/*.
    if (value.size() < prefix.size())
        return {};

    auto media_type = value.substr(0, prefix.size());

    for (std::size_t i = 0; i < media_type.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(media_type[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return {};
    }

    // Find `boundary=`.
    std::string_view parameters = value.substr(prefix.size());

    while (!parameters.empty()) {
        // Skip whitespace and semicolons.
        while (!parameters.empty() &&
               (parameters.front() == ';' || std::isspace(static_cast<unsigned char>(parameters.front())))) {
            parameters.remove_prefix(1);
        }

        if (parameters.empty())
            break;

        auto equal = parameters.find('=');

        if (equal == std::string_view::npos)
            break;

        auto name = parameters.substr(0, equal);

        // Trim whitespace around parameter name.
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.remove_suffix(1);

        std::size_t name_begin = 0;

        while (name_begin < name.size() && std::isspace(static_cast<unsigned char>(name[name_begin])))
            ++name_begin;

        name.remove_prefix(name_begin);

        parameters.remove_prefix(equal + 1);

        // Find the end of this parameter.
        std::size_t end = 0;

        if (!parameters.empty() && parameters.front() == '"') {
            // Quoted value.
            parameters.remove_prefix(1);

            end = parameters.find('"');

            if (end == std::string_view::npos)
                return {};

            auto value = parameters.substr(0, end);

            if (name.size() == 8 && std::equal(name.begin(), name.end(), "boundary", [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                })) {
                return std::string(value);
            }

            parameters.remove_prefix(end + 1);
        } else {
            end = parameters.find(';');

            auto parameter = parameters.substr(0, end == std::string_view::npos ? parameters.size() : end);

            while (!parameter.empty() && std::isspace(static_cast<unsigned char>(parameter.back())))
                parameter.remove_suffix(1);

            if (name.size() == 8 && std::equal(name.begin(), name.end(), "boundary", [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                })) {
                return std::string(parameter);
            }

            if (end == std::string_view::npos)
                break;

            parameters.remove_prefix(end);
        }
    }

    return {};
}
