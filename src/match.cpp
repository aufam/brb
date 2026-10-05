module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

module brb;

void brb::Context::match(const Router &r, std::string_view path) {
    PathCursor       cursor{path};
    std::string_view middleware = "/";
    const Router    *node       = &r;

    while (true) {
        auto &handlers = node->handlers;
        this->handlers.reserve(this->handlers.size() + handlers.size());
        for (auto &h : handlers)
            this->handlers.push_back({middleware, h});

        auto segment = cursor.next_segment();
        middleware   = cursor.consumed();
        if (segment.empty())
            break;

        if (auto it = node->children.find(segment); it != node->children.end()) {
            node = &it->second;
            continue;
        }

        if (auto &w = node->wildcard) {
            this->set(node->parameter_name, segment);
            node = w.get();
            continue;
        }

        break;
    }
}
