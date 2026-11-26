module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>

export module brb.multipart;
import brb;

export namespace brb::multipart {
    struct Part {
        fields                          headers;
        std::variant<std::string, file> body;

        size_t size(std::string_view boundary, bool more = true) const;

        template <bool isRequest>
        awaitable<void> write(
            std::string_view                          boundary,
            tcp_stream                               &stream,
            http::message<isRequest, buffer_body>    &message,
            http::serializer<isRequest, buffer_body> &serializer,
            bool                                      more = true
        ) const;

        awaitable<void> write(std::string_view boundary, Context &c, bool more = true) const {
            auto [res, ser] = c.response_buffer();
            co_await write(boundary, *c.stream, res, ser, more);
        }

        std::string dump(std::string_view boundary, bool more = true) const;
    };

    Part value(std::string_view type, std::string_view name, std::string value, std::string_view content_type = "text/plain");
    Part file(
        std::string_view      type,
        std::filesystem::path path,
        std::string_view      name         = "",
        std::string_view      filename     = "",
        std::string_view      content_type = ""
    );

    std::string get_boundary(const fields &headers);
    size_t      size_of(const std::vector<Part> &parts, std::string_view boundary);

    class Parser {
    public:
        Parser(tcp_stream &stream, flat_buffer &flat_buffer, request_parser<buffer_body> &parser, std::string_view boundary)
            : stream(stream)
            , flat_buffer(flat_buffer)
            , parser(parser)
            , boundary(std::string(boundary)) {}

        explicit Parser(Context &c)
            : stream(*c.stream)
            , flat_buffer(c.buffer)
            , parser(c.parser_buffer())
            , boundary(get_boundary(c.req())) {}

        // parsed part or error if not empty
        awaitable<std::pair<Part, std::string>> get();

        bool done() const;

    private:
        tcp_stream                  &stream;
        flat_buffer                 &flat_buffer;
        request_parser<buffer_body> &parser;

        std::string buffer;
        int         state = 0;
        std::string boundary;
        Part        part;

        std::string feed(std::string_view input);
    };
} // namespace brb::multipart


template <bool isRequest>
brb::awaitable<void> brb::multipart::Part::write(
    std::string_view                          boundary,
    tcp_stream                               &stream,
    http::message<isRequest, buffer_body>    &message,
    http::serializer<isRequest, buffer_body> &serializer,
    bool                                      more
) const {

    auto w = [&](std::string_view sv, bool more = true) -> awaitable<void> {
        message.body().data = const_cast<char *>(sv.data());
        message.body().size = sv.size();
        message.body().more = more;

        beast::error_code ec;
        co_await http::async_write(stream, serializer, asio::redirect_error(ec));
        if (ec && ec != error::need_buffer)
            throw boost::system::system_error(ec);
    };

    const std::string_view crlf = "\r\n", ddash = "--", colon = ": ";

    std::string head;
    head.reserve([&]() {
        size_t ret = 2 + boundary.size() + 2; // -- + boundary + crlf

        for (auto const &field : headers)
            ret += field.name_string().size() + 2 + field.value().size() + 2; // name + colon + value + crlf

        ret += 2;
        return ret;
    }());

    head.append(ddash);
    head.append(boundary);
    head.append(crlf);

    for (auto const &field : headers) {
        head.append(field.name_string());
        head.append(colon);
        head.append(field.value());
        head.append(crlf);
    }

    head.append(crlf);
    co_await w(head);

    if (auto f = std::get_if<brb::file>(&body)) {
        auto &file = *f;

        std::array<char, 64 * 1024> buf;
        beast::error_code           ec;

        for (;;) {
            auto n = file.read(buf.data(), buf.size(), ec);
            if (n != 0)
                co_await w({buf.data(), n});

            if (ec)
                throw beast::system_error(ec);

            if (n == 0)
                break;
        }
    } else {
        co_await w(std::get<std::string>(body));
    }

    std::string foot = std::string(crlf);
    if (!more) {
        foot.append(ddash);
        foot.append(boundary);
        foot.append(ddash);
        foot.append(crlf);
    }

    co_await w(foot, more);
}
