module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>

module brb;

bool brb::Context::is_encoded(std::string_view encoding) const {
    const auto value = req()[brb::http::field::content_encoding];

    if (value.empty())
        return false;

    auto header = std::string_view{value};

    while (!header.empty()) {
        const auto comma = header.find(',');

        auto item = brb::trim(header.substr(0, comma));
        item      = brb::trim(item);

        if (iequals(item, encoding))
            return true;

        if (comma == std::string_view::npos)
            break;

        header.remove_prefix(comma + 1);
    }

    return false;
}
