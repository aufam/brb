module;

#include <boost/beast/http.hpp>
#include <boost/asio/use_awaitable.hpp>

module brb;

static std::optional<brb::Part>
feed_part(std::string_view input, std::string &buffer, bool &headers_done, const std::string &boundary, brb::Part &part) {
    buffer.append(input);

    const std::string delimiter = "\r\n--" + boundary;

    // ------------------------------------------------------------
    // Parse part headers.
    // ------------------------------------------------------------
    if (!headers_done) {
        const auto header_end = buffer.find("\r\n\r\n");

        if (header_end == std::string::npos)
            return std::nullopt;

        std::string_view header_block{buffer.data(), header_end};

        std::size_t pos = 0;

        while (pos < header_block.size()) {
            const auto line_end = header_block.find("\r\n", pos);

            if (line_end == std::string_view::npos)
                throw std::runtime_error("Invalid multipart header");

            const auto line = header_block.substr(pos, line_end - pos);

            if (!line.empty()) {
                const auto colon = line.find(':');

                if (colon == std::string_view::npos)
                    throw std::runtime_error("Invalid multipart header");

                auto name  = line.substr(0, colon);
                auto value = line.substr(colon + 1);

                // OWS after ':'.
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                    value.remove_prefix(1);
                }

                // OWS before the end.
                while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
                    value.remove_suffix(1);
                }

                part.fields.set(name, value);
            }

            pos = line_end + 2;
        }

        // Remove:
        //
        //   headers\r\n
        //   \r\n
        //
        buffer.erase(0, header_end + 4);

        headers_done = true;
    }

    // ------------------------------------------------------------
    // Find the next part boundary.
    //
    // We search for:
    //
    //   \r\n--boundary
    //
    // because the CRLF immediately before the boundary belongs to
    // the multipart framing, not the part body.
    // ------------------------------------------------------------

    const auto boundary_pos = buffer.find(delimiter);

    if (boundary_pos == std::string::npos) {
        // The boundary may be split across the next input chunk.
        //
        // Keep enough bytes at the end of `buffer` to detect such
        // a split boundary.
        const auto keep = delimiter.size() - 1;

        if (buffer.size() > keep) {
            const auto n = buffer.size() - keep;

            part.body.append(buffer.data(), n);
            buffer.erase(0, n);
        }

        return std::nullopt;
    }

    // Everything before "\r\n--boundary" is body.
    part.body.append(buffer.data(), boundary_pos);

    // Remove body + "\r\n--boundary".
    buffer.erase(0, boundary_pos + delimiter.size());

    // At this point buffer must contain either:
    //
    //   \r\n       -> another part follows
    //
    // or
    //
    //   --        -> final boundary
    //
    if (buffer.size() < 2)
        return std::nullopt;

    if (buffer.compare(0, 2, "--") == 0) {
        // Final boundary:
        //
        // \r\n--boundary--
        //
        buffer.erase(0, 2);

        return brb::Part{
            .fields = std::move(part.fields),
            .body   = std::move(part.body),
        };
    }

    if (buffer.compare(0, 2, "\r\n") == 0) {
        // Another part follows.
        buffer.erase(0, 2);

        return brb::Part{
            .fields = std::move(part.fields),
            .body   = std::move(part.body),
        };
    }

    throw std::runtime_error("Invalid multipart boundary");
}

auto brb::Context::parse_multipart() -> awaitable<Part> {
    auto &parser = parser_buffer();

    char         buffer[8192];
    flat_buffer &flat_buffer = this->buffer;

    for (;;) {
        parser.get().body().data = buffer;
        parser.get().body().size = sizeof(buffer);

        co_await http::async_read_some(*stream, flat_buffer, parser);

        std::string_view input(buffer, sizeof(buffer) - parser.get().body().size);
        if (auto result = feed_part(input, stream_buffer, part_state, multipart_boundary, part))
            co_return std::move(*result);

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
