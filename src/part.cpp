module;

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

module brb;


enum MultipartState {
    InitialBoundary = 0,
    Headers,
    Body,
    BoundarySuffix,
    Done,
};

static std::optional<brb::Part>
feed_part(std::string_view input, std::string &buffer, int &state, const std::string &boundary, brb::Part &part) {
    buffer.append(input);

    const std::string initial = "--" + boundary;
    const std::string marker  = "\n--" + boundary;

    for (;;) {
        // --------------------------------------------------------
        // Initial boundary.
        // --------------------------------------------------------
        if (state == MultipartState::InitialBoundary) {
            if (buffer.size() < initial.size()) {
                if (initial.compare(0, buffer.size(), buffer) != 0)
                    throw std::runtime_error("Invalid initial multipart boundary: buffer = " + buffer);

                return std::nullopt;
            }

            if (buffer.compare(0, initial.size(), initial) != 0)
                throw std::runtime_error("Invalid initial multipart boundary: buffer = " + buffer);

            buffer.erase(0, initial.size());

            if (buffer.size() < 1)
                return std::nullopt;

            if (buffer.starts_with("--")) {
                buffer.erase(0, 2);
                state = MultipartState::Done;
                return std::nullopt;
            }

            if (buffer.starts_with("\r\n")) {
                buffer.erase(0, 2);
            } else if (buffer.starts_with("\n")) {
                buffer.erase(0, 1);
            } else {
                if (buffer == "\r")
                    return std::nullopt;

                throw std::runtime_error("Invalid initial multipart boundary: buffer = " + buffer);
            }

            state = MultipartState::Headers;
        }

        // --------------------------------------------------------
        // Part headers.
        // --------------------------------------------------------
        if (state == MultipartState::Headers) {
            auto        header_end     = buffer.find("\r\n\r\n");
            std::size_t separator_size = 4;

            const auto lf_end = buffer.find("\n\n");

            if (lf_end != std::string::npos && (header_end == std::string::npos || lf_end < header_end)) {
                header_end     = lf_end;
                separator_size = 2;
            }

            if (header_end == std::string::npos)
                return std::nullopt;

            std::string_view headers{buffer.data(), header_end};

            std::size_t pos = 0;

            while (pos < headers.size()) {
                auto end = headers.find('\n', pos);

                if (end == std::string_view::npos)
                    end = headers.size();

                auto line = headers.substr(pos, end - pos);

                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);

                if (!line.empty()) {
                    const auto colon = line.find(':');

                    if (colon == std::string_view::npos)
                        throw std::runtime_error("Invalid multipart header: " + std::string(line));

                    auto name  = line.substr(0, colon);
                    auto value = line.substr(colon + 1);

                    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                        value.remove_prefix(1);

                    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
                        value.remove_suffix(1);

                    part.fields.set(name, value);
                }

                pos = end + (end < headers.size() ? 1 : 0);
            }

            buffer.erase(0, header_end + separator_size);
            state = MultipartState::Body;
        }

        // --------------------------------------------------------
        // Part body.
        // --------------------------------------------------------
        if (state == MultipartState::Body) {
            const auto pos = buffer.find(marker);

            if (pos == std::string::npos) {
                // Keep enough bytes to detect a split boundary.
                // One extra byte preserves a possible CR before LF.
                const auto keep = marker.size();

                if (buffer.size() > keep) {
                    const auto n = buffer.size() - keep;

                    part.body.append(buffer.data(), n);
                    buffer.erase(0, n);
                }

                return std::nullopt;
            }

            const auto suffix = pos + marker.size();

            // Wait until we can determine whether this is a
            // valid boundary.
            if (suffix >= buffer.size())
                return std::nullopt;

            const auto next = buffer[suffix];

            bool        final_boundary = false;
            std::size_t suffix_size    = 0;

            if (next == '-') {
                if (buffer.size() < suffix + 2)
                    return std::nullopt;

                if (buffer[suffix + 1] != '-') {
                    // Not a valid boundary. Continue searching.
                    part.body.append(buffer.data(), pos + 1);
                    buffer.erase(0, pos + 1);
                    continue;
                }

                final_boundary = true;
                suffix_size    = 2;
            } else if (next == '\n') {
                suffix_size = 1;
            } else if (next == '\r') {
                if (buffer.size() < suffix + 2)
                    return std::nullopt;

                if (buffer[suffix + 1] != '\n') {
                    part.body.append(buffer.data(), pos + 1);
                    buffer.erase(0, pos + 1);
                    continue;
                }

                suffix_size = 2;
            } else {
                // Boundary-like text inside the actual body.
                part.body.append(buffer.data(), pos + 1);
                buffer.erase(0, pos + 1);
                continue;
            }

            // Exclude the framing newline from the part body.
            auto body_size = pos;

            if (body_size > 0 && buffer[body_size - 1] == '\r')
                --body_size;

            part.body.append(buffer.data(), body_size);

            buffer.erase(0, suffix + suffix_size);

            state = final_boundary ? MultipartState::Done : MultipartState::Headers;

            return brb::Part{
                .fields = std::move(part.fields),
                .body   = std::move(part.body),
            };
        }

        // --------------------------------------------------------
        // Done.
        // --------------------------------------------------------
        if (state == MultipartState::Done)
            return std::nullopt;
    }
}

auto brb::Context::parse_multipart() -> awaitable<Part> {
    auto &parser = parser_string();

    // char buffer[8192];

    flat_buffer &flat_buffer = this->buffer;

    for (;;) {
        // parser.get().body().data = buffer;
        // parser.get().body().size = sizeof(buffer);

        boost::system::error_code ec;

        co_await http::async_read_some(*stream, flat_buffer, parser, boost::asio::redirect_error(ec));

        if (ec && ec != http::error::need_buffer)
            throw boost::system::system_error(ec);

        // std::string_view input(buffer, sizeof(buffer) - parser.get().body().size());

        if (auto result = feed_part(parser.get().body(), stream_buffer, part_state, multipart_boundary, part)) {
            part = {};
            co_return std::move(*result);
        }

        if (part_state == MultipartState::Done)
            throw std::runtime_error("No more multipart parts");

        if (parser.is_done())
            throw std::runtime_error("Multipart stream ended before a complete part");
    }
}

std::string brb::Part::dump(const std::string &boundary) const {
    std::string out;

    // Boundary line
    out += "--";
    out += boundary;
    out += "\r\n";

    // Part headers
    for (auto const &field : fields) {
        out += field.name_string();
        out += ": ";
        out += field.value();
        out += "\r\n";
    }

    // Header/body separator
    out += "\r\n";

    // Body
    out += body;

    // Separator before the next part
    out += "\r\n";

    return out;
}

std::string brb::Part::dump_end(const std::string &boundary) {
    std::string out;
    out += "--";
    out += boundary;
    out += "--";
    out += "\r\n";
    return out;
}
