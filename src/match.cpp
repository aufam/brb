module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>

module brb;
namespace fs = std::filesystem;

std::pair<std::string, bool> match_filepath(const std::string &root_uri, const std::string &root_fs, const std::string &uri) {
    if (!uri.starts_with(root_uri))
        return {};

    if (uri.size() > root_uri.size() && !root_uri.ends_with('/') && uri[root_uri.size()] != '/')
        return {};

    std::error_code ec;
    const auto      root = fs::weakly_canonical(root_fs, ec);
    if (ec)
        return {};

    auto path = fs::weakly_canonical(root / uri.substr(root_uri.size()), ec);
    if (ec)
        return {};

    if (auto rel = path.lexically_relative(root); rel.empty() || rel.native().starts_with(".."))
        return {};

    if (fs::is_directory(path, ec)) {
        if (!uri.ends_with('/'))
            return {uri + '/', true}; // redirect to uri + /

        path /= "index.html";
    } else if (ec) {
        return {};
    }

    if (!fs::is_regular_file(path, ec) || ec)
        return {};

    return {path.string(), false};
}

void brb::Router::match(Context &ctx) const {
    const auto  method   = std::string(ctx.req().method_string());
    const auto &url_path = ctx.url.path();

    std::scoped_lock<std::mutex> lock(_mtx);
    ctx.handlers.reserve(middlewares.size() + 1);
    for (const auto &[path, fn] : middlewares) {
        if (url_path.starts_with(path))
            ctx.handlers.push_back(fn);
    }

    auto fn = [&]() -> Handler {
        auto key = method + " " + url_path;
        auto it  = handlers.find(key);
        if (it != handlers.end())
            return it->second;

        key = url_path;
        it  = handlers.find(key);
        if (it != handlers.end())
            return it->second;

        if (method != "GET")
            return [this](Context &c) -> awaitable<void> { co_await not_found(c); };

        for (const auto &[root_uri, root_fs] : file_handlers) {
            auto [path, redirect] = match_filepath(root_uri, root_fs, url_path);
            if (path.empty())
                continue;

            if (!redirect)
                return handle_file(ctx, path);

            auto query = std::string(ctx.url.encoded_query());
            return [=](Context &c) -> asio::awaitable<void> {
                auto &res = c.response_empty();
                auto  uri = path + (query.empty() ? "" : "?") + query;
                res.result(http::status::moved_permanently);
                res.set(http::field::location, uri);
                res.prepare_payload();
                co_await http::async_write(*c.stream, res);
            };
        }

        return [this](Context &c) -> awaitable<void> { co_await not_found(c); };
    }();

    ctx.handlers.push_back(std::move(fn));
}
