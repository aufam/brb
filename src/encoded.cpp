module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>

module brb;

static std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }

    return value;
}

static bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }

    return true;
}

bool brb::Context::is_encoded(std::string_view encoding) const {
    const auto value = req()[brb::http::field::content_encoding];

    if (value.empty())
        return false;

    auto header = std::string_view{value};

    while (!header.empty()) {
        const auto comma = header.find(',');

        auto item = trim(header.substr(0, comma));
        item      = trim(item);

        if (iequals(item, encoding))
            return true;

        if (comma == std::string_view::npos)
            break;

        header.remove_prefix(comma + 1);
    }

    return false;
}
