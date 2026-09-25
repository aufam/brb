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

static bool q_zero(std::string_view params) {
    while (!params.empty()) {
        const auto semi = params.find(';');

        auto       param = trim(params.substr(0, semi));
        const auto eq    = param.find('=');

        if (eq != std::string_view::npos) {
            auto name  = trim(param.substr(0, eq));
            auto value = trim(param.substr(eq + 1));

            if (iequals(name, "q") && !value.empty() && value.front() == '0')
                return true;
        }

        if (semi == std::string_view::npos)
            break;

        params.remove_prefix(semi + 1);
    }

    return false;
}

static bool matches_media_type(std::string_view accepted, std::string_view requested) {
    if (iequals(accepted, requested))
        return true;

    // application/* matches application/json
    if (accepted.ends_with("/*")) {
        const auto slash = accepted.find('/');
        if (slash != std::string_view::npos) {
            return requested.size() > slash && iequals(accepted.substr(0, slash), requested.substr(0, slash));
        }
    }

    // */* matches everything
    return accepted == "*/*";
}

static bool matches_encoding(std::string_view accepted, std::string_view requested) {
    return iequals(accepted, requested) || accepted == "*";
}

template <typename Matcher>
static bool accepts_header(std::string_view header, std::string_view requested, Matcher matcher) {
    while (!header.empty()) {
        const auto comma = header.find(',');

        auto       item = trim(header.substr(0, comma));
        const auto semi = item.find(';');

        auto value = trim(item.substr(0, semi));

        auto params = semi == std::string_view::npos ? std::string_view{} : item.substr(semi + 1);

        if (!q_zero(params) && matcher(value, requested))
            return true;

        if (comma == std::string_view::npos)
            break;

        header.remove_prefix(comma + 1);
    }

    return false;
}

bool brb::Context::accepts(std::string_view media_type) const {
    const auto value = req()[brb::http::field::accept];

    if (value.empty())
        return false;

    return accepts_header(value, media_type, matches_media_type);
}

bool brb::Context::accepts_encoding(std::string_view encoding) const {
    const auto value = req()[brb::http::field::accept_encoding];

    if (value.empty())
        return false;

    return accepts_header(value, encoding, matches_encoding);
};
