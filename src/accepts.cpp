module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>

module brb;


std::string_view brb::trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }

    return value;
}

bool brb::iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }

    return true;
}

static bool q_zero(const brb::ParsedOptionsHeader &parsed) {
    auto it = parsed.params.find("q");

    return it != parsed.params.end() && (it->second == "0" || it->second.starts_with("0."));
}

static bool matches_media_type(std::string_view accepted, std::string_view requested) {
    if (brb::iequals(accepted, requested))
        return true;

    // application/* matches application/json
    if (accepted.ends_with("/*")) {
        const auto slash = accepted.find('/');
        if (slash != std::string_view::npos) {
            return requested.size() > slash && brb::iequals(accepted.substr(0, slash), requested.substr(0, slash));
        }
    }

    // */* matches everything
    return accepted == "*/*";
}

static bool matches_encoding(std::string_view accepted, std::string_view requested) {
    return brb::iequals(accepted, requested) || accepted == "*";
}

template <typename Matcher>
static bool accepts_header(std::string_view header, std::string_view requested, Matcher matcher) {
    while (!header.empty()) {
        auto parsed = brb::parse_options_header(header);

        if (parsed.value.empty() && parsed.next.empty())
            break;

        if (!q_zero(parsed) && matcher(parsed.value, requested))
            return true;

        header = parsed.next;
    }

    return false;
}

bool brb::Context::accepts(std::string_view media_type) const {
    const auto value = req()[brb::http::field::accept];
    return !value.empty() && accepts_header(value, media_type, matches_media_type);
}

bool brb::Context::accepts_encoding(std::string_view encoding) const {
    const auto value = req()[brb::http::field::accept_encoding];
    return !value.empty() && accepts_header(value, encoding, matches_encoding);
}
