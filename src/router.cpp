module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

module brb;

auto brb::Router::group(std::string_view pattern) -> Router & {
    PathCursor path(pattern);
    auto       segment = path.next_segment();

    if (!segment.has_value()) {
        return *this;
    }

    if (auto &s = *segment; s.starts_with(':')) {
        parameter_name = s;

        if (!wildcard)
            wildcard = std::make_unique<Router>();

        return wildcard->group(path.remaining());
    } else {
        auto &child = children[s];
        return child.group(path.remaining());
    }
}
