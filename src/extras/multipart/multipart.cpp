module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

module brb.multipart;


namespace {
    enum MultipartState {
        InitialBoundary = 0,
        BoundarySuffix,
        Headers,
        Body,
        Done,
    };
}

bool brb::multipart::Parser::done() const {
    return state == MultipartState::Done;
}

auto brb::multipart::Parser::get() -> awaitable<std::pair<Part, std::string>> {
    char buffer[8192];

    tcp_stream  &stream      = *c.stream;
    flat_buffer &flat_buffer = c.buffer;
    auto        &parser      = c.parser_buffer();

    std::pair<Part, std::string> ret;
    for (;;) {
        if (done()) {
            ret.second = "No more multipart parts";
            co_return std::move(ret);
        }

        parser.get().body().data = buffer;
        parser.get().body().size = sizeof(buffer);

        boost::system::error_code ec;

        co_await async_read_some(stream, flat_buffer, parser, boost::asio::redirect_error(ec));
        if (ec && ec != http::error::need_buffer)
            throw boost::system::system_error(ec);

        /// bytes placed into the body buffer by this read; the async_read_some
        /// return value counts wire bytes (e.g. chunk framing), not body bytes
        const std::string_view input(buffer, sizeof(buffer) - parser.get().body().size);

        auto result = feed(input);
        if (result.empty())
            continue;

        if (result == "ok") {
            ret.first = std::move(part);
            co_return std::move(ret);
        } else {
            ret.second = std::move(result);
            co_return std::move(ret);
        }
    }
}

std::string brb::multipart::Parser::feed(std::string_view input) {
    // Epilogue after the closing boundary is ignored.
    if (state == MultipartState::Done) {
        buffer.clear();
        return {};
    }

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
                    return "Invalid initial multipart boundary: buffer = " + buffer;

                return {};
            }

            if (buffer.compare(0, initial.size(), initial) != 0)
                return "Invalid initial multipart boundary: buffer = " + buffer;

            buffer.erase(0, initial.size());

            // The boundary is consumed, so we must not stay in this
            // state or the next call would look for it again.
            state = MultipartState::BoundarySuffix;
        }

        // --------------------------------------------------------
        // Suffix after the initial boundary: "--", CRLF or LF.
        // --------------------------------------------------------
        if (state == MultipartState::BoundarySuffix) {
            if (buffer.empty())
                return {};

            if (buffer[0] == '-') {
                if (buffer.size() < 2)
                    return {};

                if (buffer[1] != '-')
                    return "Invalid initial multipart boundary suffix";

                state = MultipartState::Done;
                buffer.clear();
                return {};
            }

            if (buffer[0] == '\n') {
                buffer.erase(0, 1);
            } else if (buffer[0] == '\r') {
                if (buffer.size() < 2)
                    return {};

                if (buffer[1] != '\n')
                    return "Invalid initial multipart boundary suffix";

                buffer.erase(0, 2);
            } else {
                return "Invalid initial multipart boundary suffix";
            }

            state = MultipartState::Headers;
        }

        // --------------------------------------------------------
        // Part headers.
        // --------------------------------------------------------
        if (state == MultipartState::Headers) {
            if (buffer.empty())
                return {};

            if (buffer[0] == '\n') {
                // Part with no headers.
                buffer.erase(0, 1);
                state = MultipartState::Body;
            } else if (buffer[0] == '\r' && buffer.size() < 2) {
                return {};
            } else if (buffer.starts_with("\r\n")) {
                // Part with no headers.
                buffer.erase(0, 2);
                state = MultipartState::Body;
            } else {
                auto        header_end     = buffer.find("\r\n\r\n");
                std::size_t separator_size = 4;

                const auto lf_end = buffer.find("\n\n");

                if (lf_end != std::string::npos && (header_end == std::string::npos || lf_end < header_end)) {
                    header_end     = lf_end;
                    separator_size = 2;
                }

                if (header_end == std::string::npos)
                    return {};

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
                            return "Invalid multipart header: line = " + std::string(line);

                        auto name  = line.substr(0, colon);
                        auto value = line.substr(colon + 1);

                        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                            value.remove_prefix(1);

                        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
                            value.remove_suffix(1);

                        part.headers.set(name, value);
                    }

                    pos = end + (end < headers.size() ? 1 : 0);
                }

                buffer.erase(0, header_end + separator_size);
                state = MultipartState::Body;
            }
        }

        // --------------------------------------------------------
        // Part body.
        // --------------------------------------------------------
        if (state == MultipartState::Body) {
            const auto pos = buffer.find(marker);

            if (pos == std::string::npos) {
                // Keep enough bytes to detect a split boundary, plus one
                // extra byte so a CR in front of the LF is never flushed.
                const auto keep = marker.size() + 1;

                if (buffer.size() > keep) {
                    const auto n = buffer.size() - keep;

                    part.body.append(buffer.data(), n);
                    buffer.erase(0, n);
                }

                return {};
            }

            const auto suffix = pos + marker.size();

            // Wait until we can determine whether this is a valid boundary.
            if (suffix >= buffer.size())
                return {};

            const auto next = buffer[suffix];

            bool        final_boundary = false;
            std::size_t suffix_size    = 0;

            if (next == '-') {
                if (buffer.size() < suffix + 2)
                    return {};

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
                    return {};

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

            if (final_boundary)
                buffer.clear(); // discard epilogue

            return "ok";
        }

        // --------------------------------------------------------
        // Done.
        // --------------------------------------------------------
        if (state == MultipartState::Done)
            return {};

        return "Invalid multipart parser state";
    }
}


std::string brb::multipart::Part::dump(const std::string &boundary) const {
    std::string out;

    // Boundary line
    out += "--";
    out += boundary;
    out += "\r\n";

    // Part headers
    for (auto const &field : headers) {
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

std::string brb::multipart::Part::dump_end(const std::string &boundary) {
    std::string out;
    out += "--";
    out += boundary;
    out += "--";
    out += "\r\n";
    return out;
}
