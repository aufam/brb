module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

module brb;

void brb::Router::match(Context &ctx) const {
    const auto  method   = std::string(ctx.req().method_string());
    const auto &url_path = ctx.url.path();

    std::scoped_lock<std::mutex> lock(_mtx);

    ctx.handlers.reserve(middlewares.size() + 1);
    ctx.handler_paths.reserve(middlewares.size() + 1);
    for (const auto &[path, fn] : middlewares) {
        if (url_path.starts_with(path)) {
            ctx.handlers.push_back(fn);
            ctx.handler_paths.push_back(path);
        }
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

        return [this](Context &c) -> awaitable<void> { co_await not_found(c); };
    }();

    ctx.handlers.push_back(std::move(fn));
    ctx.handler_paths.push_back(url_path);
}
