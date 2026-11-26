module;

#include <string>
#include <unordered_map>

module brb;

brb::ParsedOptionsHeader brb::parse_options_header(std::string_view sv) noexcept {
    ParsedOptionsHeader result;

    auto skip_ows = [&] {
        while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t'))
            sv.remove_prefix(1);
    };

    auto is_token = [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '!' || c == '#' || c == '$' ||
               c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' || c == '^' || c == '_' ||
               c == '`' || c == '|' || c == '~';
    };

    // Skip malformed input until the next delimiter.
    auto skip_to_delimiter = [&] {
        bool quoted  = false;
        bool escaped = false;

        while (!sv.empty()) {
            char c = sv.front();
            sv.remove_prefix(1);

            if (escaped) {
                escaped = false;
                continue;
            }

            if (quoted && c == '\\') {
                escaped = true;
                continue;
            }

            if (c == '"') {
                quoted = !quoted;
                continue;
            }

            if (!quoted && (c == ';' || c == ','))
                return c;
        }

        return '\0';
    };

    skip_ows();

    // Parse the primary value.
    auto start = sv.data();

    while (!sv.empty() && sv.front() != ';' && sv.front() != ',')
        sv.remove_prefix(1);

    auto end = sv.data();

    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
        --end;

    result.value = {start, static_cast<size_t>(end - start)};

    // Parse parameters.
    while (!sv.empty()) {
        skip_ows();

        if (sv.empty())
            break;

        if (sv.front() == ',') {
            sv.remove_prefix(1);
            skip_ows();
            result.next = sv;
            break;
        }

        if (sv.front() != ';')
            break;

        sv.remove_prefix(1);
        skip_ows();

        // Parameter name.
        auto name_start = sv.data();

        while (!sv.empty() && is_token(static_cast<unsigned char>(sv.front())))
            sv.remove_prefix(1);

        if (sv.empty() || sv.front() != '=') {
            auto delimiter = skip_to_delimiter();

            if (delimiter == ',') {
                skip_ows();
                result.next = sv;
                break;
            }

            continue;
        }

        std::string_view name{name_start, static_cast<size_t>(sv.data() - name_start)};

        // '=' must not have whitespace around it.
        sv.remove_prefix(1);

        if (sv.empty())
            break;

        std::string value;

        if (sv.front() == '"') {
            // Quoted parameter value.
            sv.remove_prefix(1);

            bool closed = false;

            while (!sv.empty()) {
                char c = sv.front();
                sv.remove_prefix(1);

                if (c == '"') {
                    closed = true;
                    break;
                }

                if (c == '\\' && !sv.empty()) {
                    c = sv.front();
                    sv.remove_prefix(1);
                }

                value.push_back(c);
            }

            if (!closed)
                break;
        } else {
            // Token parameter value.
            auto value_start = sv.data();

            while (!sv.empty() && is_token(static_cast<unsigned char>(sv.front())))
                sv.remove_prefix(1);

            value.assign(value_start, static_cast<size_t>(sv.data() - value_start));
        }

        result.params.insert_or_assign(name, std::move(value));

        skip_ows();

        if (sv.empty())
            break;

        if (sv.front() == ';')
            continue;

        if (sv.front() == ',') {
            sv.remove_prefix(1);
            skip_ows();
            result.next = sv;
            break;
        }

        auto delimiter = skip_to_delimiter();

        if (delimiter == ',') {
            skip_ows();
            result.next = sv;
            break;
        }
    }

    return result;
}
