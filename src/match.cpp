module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

module brb;

auto brb::Context::next() -> awaitable<void> {
    if (aborted || idx >= handlers.size())
        co_return;

    const auto mp = matched_path;

    auto [path, fn] = handlers[idx++];
    defer _         = [&]() { matched_path = mp; };

    matched_path = path;
    co_await fn(*this);
}

void brb::Context::match(const Router &r) {
    PathCursor    cursor(url_path);
    const Router *node = &r;

    while (true) {
        handlers.reserve(handlers.size() + node->middleware.size());
        for (auto &h : node->middleware)
            handlers.push_back({cursor.consumed(), h});

        auto segment = cursor.next_segment();
        if (!segment.has_value()) {
            if (auto it = node->routes.find(req().method()); it != node->routes.end()) {
                handlers.reserve(handlers.size() + it->second.size());
                for (auto &h : it->second)
                    handlers.push_back({cursor.consumed(), h});
            } else if (auto it = node->routes.find(http::verb::unknown); it != node->routes.end()) {
                handlers.reserve(handlers.size() + it->second.size());
                for (auto &h : it->second)
                    handlers.push_back({cursor.consumed(), h});
            }
            break;
        }

        if (auto it = node->children.find(*segment); it != node->children.end()) {
            node = &it->second;
            continue;
        }

        if (auto &w = node->wildcard) {
            this->set(node->parameter_name, *segment);
            node = w.get();
            continue;
        }

        break;
    }
}
