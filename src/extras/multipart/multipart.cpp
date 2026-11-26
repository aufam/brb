module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>

module brb.multipart;


namespace {
    enum MultipartState {
        InitialBoundary = 0,
        BoundarySuffix,
        Headers,
        Body,
        Done,
    };

    struct BodyAppend {
        const char *data;
        size_t      len;

        void operator()(std::string &body) {
            body.append(data, len);
        }

        void operator()(brb::file &body) {
            boost::beast::error_code ec;
            body.write(data, len, ec);

            if (ec)
                throw boost::beast::system_error(ec);
        }
    };

    struct BodyRead {
        std::string &out;

        void operator()(const std::string &body) {
            out += body;
        }

        void operator()(const brb::file &body) {
            boost::beast::error_code ec;

            std::string temp;
            temp.resize(body.size(ec));

            if (ec)
                throw boost::beast::system_error(ec);

            body.read(temp.data(), temp.size(), ec);

            if (ec)
                throw boost::beast::system_error(ec);

            out += temp;
        }
    };

    struct BodyLen {
        size_t operator()(const std::string &body) {
            return body.size();
        }

        size_t operator()(const brb::file &body) {
            boost::beast::error_code ec;

            auto ret = body.size(ec);
            if (ec)
                throw boost::beast::system_error(ec);

            return ret;
        }
    };
} // namespace

size_t brb::multipart::Part::size(std::string_view boundary, bool more) const {
    size_t ret = 2 + boundary.size() + 2; // -- + boundary + crlf

    for (auto const &field : headers)
        ret += field.name_string().size() + 2 + field.value().size() + 2; // name + colon + value + crlf

    ret += 2 + std::visit(BodyLen{}, body) + 2; // crlf + body + crlf

    if (!more)
        ret = 2 + boundary.size() + 2 + 2; // -- + boundary + -- + crlf

    return ret;
}

bool brb::multipart::Parser::done() const {
    return state == MultipartState::Done;
}

auto brb::multipart::Parser::get() -> awaitable<std::pair<Part, std::string>> {
    char buffer[8192];

    std::pair<Part, std::string> ret;
    for (;;) {
        if (done()) {
            ret.second = "No more multipart parts";
            co_return std::move(ret);
        }

        parser.get().body().data = buffer;
        parser.get().body().size = sizeof(buffer);

        boost::system::error_code ec;

        auto len = co_await async_read_some(stream, flat_buffer, parser, boost::asio::redirect_error(ec));
        if (ec && ec != http::error::need_buffer)
            throw boost::system::system_error(ec);

        /// bytes placed into the body buffer by this read; the async_read_some
        /// return value counts wire bytes (e.g. chunk framing), not body bytes
        const std::string_view input(buffer, len);

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

                    std::visit(BodyAppend{buffer.data(), n}, part.body);
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
                    std::visit(BodyAppend{buffer.data(), pos + 1}, part.body);
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
                    std::visit(BodyAppend{buffer.data(), pos + 1}, part.body);
                    buffer.erase(0, pos + 1);
                    continue;
                }

                suffix_size = 2;
            } else {
                // Boundary-like text inside the actual body.
                std::visit(BodyAppend{buffer.data(), pos + 1}, part.body);
                buffer.erase(0, pos + 1);
                continue;
            }

            // Exclude the framing newline from the part body.
            auto body_size = pos;

            if (body_size > 0 && buffer[body_size - 1] == '\r')
                --body_size;

            std::visit(BodyAppend{buffer.data(), body_size}, part.body);

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

std::string brb::multipart::Part::dump(std::string_view boundary, bool more) const {
    std::string out;
    out.reserve(size(boundary, more));

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

    std::visit(BodyRead{out}, body);

    // Separator before the next part
    out += "\r\n";

    if (!more) {
        out += "--";
        out += boundary;
        out += "--";
        out += "\r\n";
    }

    return out;
}

std::string brb::multipart::get_boundary(const fields &headers) {
    auto [value, params, _] = brb::parse_options_header(headers[field::content_type]);

    if (!value.starts_with("multipart/"))
        return {};

    return params["boundary"];
}

size_t brb::multipart::size_of(const std::vector<Part> &parts, std::string_view boundary) {
    size_t ret  = 0;
    bool   more = false;

    for (size_t i = parts.size(); i > 0; ++i) {
        ret += parts[i - 1].size(boundary, more);
        more = true;
    }

    return ret;
}

auto brb::multipart::value(std::string_view type, std::string_view name, std::string value, std::string_view content_type)
    -> Part {
    std::string content_disposition = std::string(type);
    if (!name.empty()) {
        content_disposition += "; \"";
        content_disposition += name;
        content_disposition += '"';
    }

    Part part;
    part.headers.set(field::content_disposition, content_disposition);
    part.headers.set(field::content_type, content_type);
    part.body = std::move(value);

    return part;
}

auto brb::multipart::file(
    std::string_view      type,
    std::filesystem::path path,
    std::string_view      name,
    std::string_view      filename,
    std::string_view      content_type
) -> Part {
    std::string fname = filename.empty() ? path.filename().string() : std::string(filename);
    std::string content_disposition(type);

    if (!name.empty()) {
        content_disposition += "; name=\"";
        content_disposition += name;
        content_disposition += '"';
    }

    if (!fname.empty()) {
        content_disposition += "; filename=\"";
        content_disposition += fname;
        content_disposition += '"';
    }

    std::string path_str = path.string();
    std::string ext_str  = path.extension().string();

    brb::file                 file;
    boost::system::error_code ec;

    file.open(path_str.c_str(), file_mode::scan, ec);
    if (ec)
        throw boost::system::system_error(ec);

    Part part;
    part.headers.set(field::content_disposition, content_disposition);
    part.headers.set(field::content_type, content_type.empty() ? brb::mime_type(ext_str) : content_type);
    part.body = std::move(file);

    return part;
}
