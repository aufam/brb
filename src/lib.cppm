module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>

export module brb;

export namespace brb {
    struct Engine;
    struct Router;
    struct Context;
    struct SSE;
    struct Part;
    using Handler = std::function<boost::asio::awaitable<void>(Context &)>;

    extern "C++" const char *version();

    namespace asio  = boost::asio;
    namespace beast = boost::beast;
    namespace http  = boost::beast::http;
    namespace ws    = boost::beast::websocket;
    namespace urls  = boost::urls;

    using boost::asio::awaitable;
    using boost::asio::ip::tcp;
    using boost::beast::flat_buffer;

    using boost::beast::tcp_stream;
    using ws_stream = boost::beast::websocket::stream<tcp_stream>;

    using request_header  = boost::beast::http::request_header<boost::beast::http::fields>;
    using response_header = boost::beast::http::response_header<boost::beast::http::fields>;

    using boost::beast::http::buffer_body;
    using boost::beast::http::empty_body;
    using boost::beast::http::file_body;
    using boost::beast::http::string_body;

    using boost::beast::http::message;
    using boost::beast::http::parser;
    using boost::beast::http::serializer;

    using boost::beast::http::request;
    using boost::beast::http::request_parser;
    using boost::beast::http::request_serializer;
    using boost::beast::http::response;
    using boost::beast::http::response_parser;
    using boost::beast::http::response_serializer;

    using boost::beast::http::field;
    using boost::beast::http::fields;
    using boost::beast::http::status;
    using boost::beast::http::verb;

    using boost::beast::http::string_to_field;
    using boost::beast::http::string_to_verb;
    using boost::beast::http::to_string;

    using boost::beast::http::async_read;
    using boost::beast::http::async_read_some;
    using boost::beast::http::async_write;
    using boost::beast::http::async_write_some;
    using boost::beast::http::read;
    using boost::beast::http::read_some;
    using boost::beast::http::write;
    using boost::beast::http::write_some;

    using boost::beast::http::async_read_header;
    using boost::beast::http::async_write_header;
    using boost::beast::http::read_header;
    using boost::beast::http::write_header;

    using URL = boost::urls::url;
} // namespace brb

namespace brb {
    template <typename F>
    class defer;

    struct PathCursor;
} // namespace brb

template <typename F>
class brb::defer {
public:
    defer(F fn)
        : fn(std::move(fn)) {}
    F fn;
    ~defer() {
        fn();
    }
};

struct brb::PathCursor {
    explicit PathCursor(std::string_view path)
        : path(path) {
        if (path.starts_with('/') || path.empty()) {
            offset++;
        }
    }

    const std::string_view path;
    std::size_t            offset = 0;

    std::string_view remaining() const {
        return offset > path.size() ? "" : path.substr(offset);
    }

    std::string_view consumed() const {
        return offset > path.size() ? path : path.substr(0, offset);
    }

    std::optional<std::string_view> next_segment() {
        if (offset > path.size())
            return std::nullopt;

        const auto start = offset;
        const auto pos   = path.find('/', start);

        if (pos == std::string_view::npos) {
            offset = path.size() + 1;
            return path.substr(start);
        }

        offset = pos + 1;
        return path.substr(start, pos - start);
    }
};

struct brb::SSE {
    std::string                              event;
    std::string                              data;
    std::string                              id;
    std::optional<std::chrono::milliseconds> retry;

    std::string dump() const;
};

struct brb::Part {
    http::fields fields;
    std::string  body;

    std::string        dump(const std::string &boundary) const;
    static std::string dump_end(const std::string &boundary);
};

struct brb::Context {
    friend Engine;

    std::shared_ptr<tcp_stream> stream;
    flat_buffer                 buffer;             ///< request buffer
    URL                         url;                ///< parsed url from request header
    std::string                 url_path;           ///< string view to decoded url path
    std::string                 multipart_boundary; ///< parsed multipart_boundary
    std::string_view            matched_path;       ///< currently matched path that being processed by a handler

    /// set local variable for this context
    template <typename T>
    void set(std::string_view key, T val) {
        vars[std::string(key)] = std::move(val);
    }

    /// get local variable for this context
    template <typename T = std::string_view>
    decltype(auto) get(std::string_view key) const {
        auto &value = vars.at(std::string(key));

        if constexpr (std::is_same_v<T, std::string_view>) {
            std::string_view sv;
            if (value.type() == typeid(std::string)) {
                sv = std::any_cast<const std::string &>(value);
            } else if (value.type() == typeid(const char *)) {
                sv = std::any_cast<const char *>(value);
            } else {
                sv = std::any_cast<std::string_view>(value);
            }

            return sv;
        } else {
            return std::any_cast<const T &>(value);
        }
    }

    /// get local variable for this context
    template <typename T = std::string_view>
    decltype(auto) get(std::string_view key) {
        auto &value = vars.at(std::string(key));

        if constexpr (std::is_same_v<T, std::string_view>) {
            std::string_view sv;
            if (value.type() == typeid(std::string)) {
                sv = std::any_cast<const std::string &>(value);
            } else if (value.type() == typeid(const char *)) {
                sv = std::any_cast<const char *>(value);
            } else {
                sv = std::any_cast<std::string_view>(value);
            }

            return sv;
        } else {
            return std::any_cast<T &>(value);
        }
    }

    /// get local variable for this context
    template <typename T>
    T get_or(std::string_view key, T default_value) const {
        if (vars.count(std::string(key)))
            return get(key);
        return std::move(default_value);
    }

    /// returns true if the response semantic indicates keep-alive
    bool keep_alive() const {
        return std::visit([](auto &res) { return res.keep_alive(); }, r);
    }

    /// set the keep alive response semantic option
    void keep_alive(bool v) {
        std::visit([v](auto &res) { return res.keep_alive(v); }, r);
    }

    /// get parsed request header
    const request_header &req() const {
        if (parser.index() == 0) {
            return std::get<0>(parser).get().base();
        } else if (parser.index() == 1) {
            return std::get<1>(parser)->get().base();
        } else if (parser.index() == 2) {
            return std::get<2>(parser)->get().base();
        } else {
            return std::get<3>(parser)->get().base();
        }
    }

    /// get current response header
    response_header &res() {
        return std::visit([](auto &r) -> response_header & { return r.base(); }, r);
    }

    /// invoke next handler
    awaitable<void> next() {
        if (aborted || idx >= handlers.size())
            co_return;

        const auto mp = matched_path;

        auto [path, fn] = handlers[idx++];
        defer _         = [&]() { matched_path = mp; };

        matched_path = path;
        co_await fn(*this);
    }

    /// disable `c.next()` and `c.write_response()`
    void abort() {
        aborted = true;
    }

    /// Get the response as an empty-body response.
    ///
    /// If the current response uses another supported body type, its response
    /// metadata is preserved and the body is discarded.
    response<empty_body> &response_empty() {
        if (auto *p = std::get_if<response<empty_body>>(&r)) {
            return *p;
        } else if (auto *p = std::get_if<response<string_body>>(&r)) {
            r = response<empty_body>(std::move(p->base()));
        } else if (auto *p = std::get_if<response<file_body>>(&r)) {
            r = response<empty_body>(std::move(p->base()));
        } else if (std::get_if<response<buffer_body>>(&r)) {
            throw std::runtime_error("cannot convert buffer body to empty body");
        }
        return std::get<response<empty_body>>(r);
    }

    /// Get the response as a string-body response.
    ///
    /// An empty body is converted directly. A file body is read completely into
    /// memory.
    ///
    /// @throws std::runtime_error if the file cannot be read or the response body is buffer body
    response<string_body> &response_string(std::optional<std::string> body = std::nullopt) {
        if (auto *p = std::get_if<response<empty_body>>(&r)) {
            auto res = response<string_body>(std::move(*p));
            if (body.has_value())
                res.body() = std::move(*body);

            r = std::move(res);
        } else if (auto *p = std::get_if<response<string_body>>(&r)) {
            return *p;
        } else if (auto *p = std::get_if<response<file_body>>(&r)) {
            auto res = response<string_body>(std::move(p->base()));

            if (body.has_value()) {
                res.body() = std::move(*body);
            } else if (p->body().file().is_open()) {
                res.body().resize(p->body().size());

                auto ec = beast::error_code();
                p->body().file().read(res.body().data(), res.body().size(), ec);

                if (ec)
                    throw std::runtime_error(ec.message());
            }

            r = std::move(res);
        } else if (std::get_if<response<buffer_body>>(&r)) {
            throw std::runtime_error("cannot convert buffer body to string body");
        }

        return std::get<response<string_body>>(r);
    }

    /// Get the response as a file-body response.
    ///
    /// An empty body is converted directly. A string body is written to the
    /// specified file. If the current response is already a file body, an
    /// optional filepath can be used to reopen it from that file.
    ///
    /// @param filepath If not empty, path of the file to use for the response body.
    /// @throws std::runtime_error if a string body is converted without a
    ///         filepath, or if the file cannot be opened or written.
    response<file_body> &response_file(const std::string &filepath = "") {
        if (auto *p = std::get_if<response<empty_body>>(&r)) {
            auto res = response<file_body>(std::move(*p));

            if (!filepath.empty()) {
                auto ec = beast::error_code();
                res.body().open(filepath.c_str(), beast::file_mode::scan, ec);

                if (ec)
                    throw std::runtime_error(ec.message());
            }

            r = std::move(res);
        } else if (std::get_if<response<string_body>>(&r)) {
            throw std::runtime_error("cannot convert string body to file body without");
        } else if (auto *p = std::get_if<response<file_body>>(&r)) {
            if (filepath.empty())
                return *p;

            auto ec = beast::error_code();
            p->body().open(filepath.c_str(), beast::file_mode::scan, ec);

            if (ec)
                throw std::runtime_error(ec.message());

            return *p;
        } else if (std::get_if<response<buffer_body>>(&r)) {
            throw std::runtime_error("cannot convert buffer body to file body");
        }

        return std::get<response<file_body>>(r);
    }

    /// Get the response as a buffer-body response.
    ///
    /// An empty body is converted directly. Other body types cannot be converted
    /// because buffer_body does not own the memory referenced by its buffer.
    response<buffer_body> &response_buffer() {
        if (auto *p = std::get_if<response<empty_body>>(&r)) {
            r = response<buffer_body>(std::move(*p));
        } else if (std::get_if<response<string_body>>(&r)) {
            throw std::runtime_error("cannot convert string body to buffer body");
        } else if (std::get_if<response<file_body>>(&r)) {
            throw std::runtime_error("cannot convert file body to buffer body");
        } else if (auto *p = std::get_if<response<buffer_body>>(&r)) {
            return *p;
        }

        return std::get<response<buffer_body>>(r);
    }

    /// get request parser as empty-body request
    request_parser<empty_body> &parser_empty() {
        if (auto *p = std::get_if<empty_parser_t>(&parser)) {
            return *p;
        } else if (std::get_if<string_parser_t>(&parser)) {
            throw std::runtime_error("the parser is already converted to string parser");
        } else if (std::get_if<file_parser_t>(&parser)) {
            throw std::runtime_error("the parser is already converted to file parser");
        } else if (std::get_if<buffer_parser_t>(&parser)) {
            throw std::runtime_error("the parser is already converted to buffer parser");
        } else {
            throw std::runtime_error("unknown parser");
        }
    }

    /// Get the request parser as a string-body request.
    ///
    /// If the parser has not yet been materialized, it is converted to a
    /// string-body parser. A file-body parser is converted by reading its
    /// contents into the string body.
    ///
    /// @throws std::runtime_error if the parser has already been converted to
    ///         a buffer-body parser, if a completed file-body parser is being
    ///         converted, if reading the file fails, or if the parser state
    ///         is unknown.
    request_parser<string_body> &parser_string() {
        if (auto *p = std::get_if<empty_parser_t>(&parser)) {
            parser = std::make_unique<request_parser<string_body>>(std::move(*p));
        } else if (auto *p = std::get_if<string_parser_t>(&parser)) {
            return *p->get();
        } else if (auto *p = std::get_if<file_parser_t>(&parser)) {
            auto &fp = *p->get();
            if (!fp.is_done())
                throw std::runtime_error("the parser is already converted to file parser and it's not done");

            std::string body;
            body.resize(fp.get().body().size());

            auto ec = beast::error_code();
            fp.get().body().file().read(body.data(), body.size(), ec);

            if (ec)
                throw std::runtime_error(ec.message());

            auto sp          = std::make_unique<request_parser<string_body>>(std::move(fp.get().base()));
            sp->get().body() = std::move(body);
            parser           = std::move(sp);
        } else if (std::get_if<buffer_parser_t>(&parser)) {
            throw std::runtime_error("the parser is already converted to buffer parser");
        } else {
            throw std::runtime_error("unknown parser");
        }

        return *std::get<string_parser_t>(parser);
    }

    /// Get the request parser as a file-body request.
    ///
    /// If the parser has not yet been materialized, it is converted to a
    /// file-body parser. If @p filepath is non-empty, the file body is opened
    /// for writing at that path. An existing file-body parser may also be
    /// redirected to a new file when @p filepath is provided.
    ///
    /// @param filepath Path to the file used as the request body. If empty,
    ///                 no file is opened or changed.
    ///
    /// @throws std::runtime_error if the parser has already been converted to
    ///         a string-body or buffer-body parser, if opening the file fails,
    ///         or if the parser state is unknown.
    request_parser<file_body> &parser_file(const std::string filepath = "") {
        if (auto *p = std::get_if<empty_parser_t>(&parser)) {
            auto fp = std::make_unique<request_parser<file_body>>(std::move(*p));

            if (!filepath.empty()) {
                auto ec = beast::error_code();
                fp->get().body().open(filepath.c_str(), beast::file_mode::write, ec);

                if (ec)
                    throw std::runtime_error(ec.message());
            }

            parser = std::move(fp);
        } else if (std::get_if<string_parser_t>(&parser)) {
            throw std::runtime_error("the parser is already converted to string parser");
        } else if (auto *p = std::get_if<file_parser_t>(&parser)) {
            auto &fp = *p->get();
            if (filepath.empty())
                return fp;

            auto ec = beast::error_code();
            fp.get().body().open(filepath.c_str(), beast::file_mode::write, ec);

            if (ec)
                throw std::runtime_error(ec.message());

            return fp;
        } else if (std::get_if<buffer_parser_t>(&parser)) {
            throw std::runtime_error("the parser is already converted to buffer parser");
        } else {
            throw std::runtime_error("unknown parser");
        }

        return *std::get<file_parser_t>(parser);
    }

    /// get request parser as buffer-body request
    request_parser<buffer_body> &parser_buffer() {
        if (auto *p = std::get_if<buffer_parser_t>(&parser))
            return *p->get();
        parser = std::make_unique<request_parser<buffer_body>>(std::move(parser_empty()));
        return *std::get<buffer_parser_t>(parser);
    }

    bool accepts(std::string_view media_type) const;
    bool accepts_encoding(std::string_view encoding) const;
    bool is_encoded(std::string_view encoding) const;

    void append_vary(std::string_view value) {
        auto &res  = this->res();
        auto  vary = res[http::field::vary];
        if (vary.empty()) {
            res.set(http::field::vary, value);
        } else {
            res.set(http::field::vary, std::string(vary) + ", " + std::string(value));
        }
    }

    void append_vary(http::field value) {
        append_vary(http::to_string(value));
    }

    awaitable<SSE>  parse_sse();
    awaitable<Part> parse_multipart();

    /// @brief Configures the response for Server-Sent Events (SSE).
    ///
    /// Sets the content type to `text/event-stream` and enables HTTP chunked
    /// transfer encoding for streaming events.
    void set_event_stream() {
        auto &res = response_buffer();
        res.set(http::field::content_type, "text/event-stream");
        res.chunked(true);
    }

    /// @brief Configures the response for a multipart stream.
    ///
    /// Sets the content type to `multipart/mixed` with the specified boundary
    /// and enables HTTP chunked transfer encoding for streaming parts.
    ///
    /// @param boundary The boundary used to separate multipart body parts.
    void set_multipart_stream(std::string_view boundary, const std::string &multipart_mime = "mixed") {
        auto &res = response_buffer();
        res.set(http::field::content_type, "multipart/" + multipart_mime + "; boundary=" + std::string(boundary));
        res.chunked(true);
    }

    /// @brief Writes a chunk of the response body using a streaming serializer.
    ///
    //// The supplied data is written immediately and must remain valid until the
    /// asynchronous write completes. When @p more is true, the response remains
    /// open for subsequent chunks. When @p more is false, this chunk marks the
    /// end of the response body.
    ///
    /// @param data The response body data to write.
    /// @param more Whether additional body data will be written after this chunk.
    ///
    /// @throws boost::system::system_error If the write fails with an error other
    /// than http::error::need_buffer.
    awaitable<void> write_chunk(std::string_view data, bool more) {
        auto &res = response_buffer();
        if (!serializer) {
            serializer = std::make_unique<http::response_serializer<http::buffer_body>>(res);
        }

        res.body().data = const_cast<char *>(data.data());
        res.body().size = data.size();
        res.body().more = more;

        try {
            co_await http::async_write(*stream, *serializer);
        } catch (boost::system::system_error const &e) {
            if (e.code() != http::error::need_buffer)
                throw;
        }
    }

    /// Writes the stored HTTP response to the underlying stream.
    ///
    /// Prepares the payload before writing responses using `empty_body`,
    /// `string_body`, or `file_body`. Responses using `buffer_body` are not
    /// written by this function and are expected to be handled separately.
    ///
    /// @throws boost::system::system_error if the asynchronous write fails.
    awaitable<void> write_response() {
        if (aborted)
            co_return;

        if (auto *p = std::get_if<response<empty_body>>(&r)) {
            p->prepare_payload();
            co_await http::async_write(*stream, *p);
            aborted = true;
        } else if (auto *p = std::get_if<response<string_body>>(&r)) {
            p->prepare_payload();
            co_await http::async_write(*stream, *p);
            aborted = true;
        } else if (auto *p = std::get_if<response<file_body>>(&r)) {
            p->prepare_payload();
            co_await http::async_write(*stream, *p);
            aborted = true;
        } else if (std::get_if<response<buffer_body>>(&r)) {
            // do nothing
        }
    }

    void match(const Router &r);

private:
    using empty_parser_t  = request_parser<empty_body>;
    using string_parser_t = std::unique_ptr<request_parser<string_body>>;
    using file_parser_t   = std::unique_ptr<request_parser<file_body>>;
    using buffer_parser_t = std::unique_ptr<request_parser<buffer_body>>;
    using serializer_t    = std::unique_ptr<response_serializer<buffer_body>>;

    std::variant<empty_parser_t, string_parser_t, file_parser_t, buffer_parser_t>                         parser;
    std::variant<response<empty_body>, response<string_body>, response<file_body>, response<buffer_body>> r;
    serializer_t                                                                                          serializer;

    std::unordered_map<std::string, std::any>         vars;
    std::vector<std::pair<std::string_view, Handler>> handlers;
    size_t                                            idx     = 0;
    bool                                              aborted = false;

    SSE         sse;
    Part        part;
    std::string stream_buffer;

    union {
        int  part_state = 0;
        bool sse_has_data;
    };
};

export namespace brb::middlewares {
    struct request_reader {
        bool        string_body = false;
        bool        file_body   = false;
        bool        temp_file   = false;
        std::string file_path   = "";

        awaitable<void> operator()(Context &) const;
    };

    struct require_method {
        http::verb method;
        Handler    on_not_allowed = nullptr; ///< 405 if null

        awaitable<void> operator()(Context &) const;
    };

    struct require_content_type {
        std::vector<std::string> content_types;
        Handler                  on_unsupported = nullptr; ///< 415 if null

        awaitable<void> operator()(Context &) const;
    };

    struct timeout {
        std::chrono::steady_clock::duration duration;

        awaitable<void> operator()(Context &) const;
    };

    struct cors {
        std::vector<std::string> origins;
        std::vector<http::verb>  methods;
        std::vector<http::field> headers;

        std::chrono::seconds max_age{};
        bool                 credentials = false;

        awaitable<void> operator()(Context &) const;

        bool allowed_origin(std::string_view origin) const {
            if (std::ranges::find(origins, "*") != origins.end())
                return true;

            return std::ranges::find(origins, origin) != origins.end();
        }

        bool allowed_method(http::verb method) const {
            return std::ranges::find(methods, method) != methods.end();
        }

        bool allowed_header(std::string_view name) const {
            auto field = http::string_to_field(name);
            return field != http::field::unknown && std::ranges::find(headers, field) != headers.end();
        }
    };
} // namespace brb::middlewares

struct brb::Router {
    std::vector<Handler>                                 middleware;
    std::unordered_map<std::string_view, Router>         children;
    std::unique_ptr<Router>                              wildcard;
    std::string                                          parameter_name;
    std::unordered_map<http::verb, std::vector<Handler>> routes;

    template <typename... Fn>
        requires(std::convertible_to<Fn, std::function<awaitable<void>(Context &)>> && ...)
    void use(Fn... handlers) {
        (middleware.push_back(std::move(handlers)), ...);
    }

    template <typename... Fn>
        requires(std::convertible_to<Fn, std::function<awaitable<void>(Context &)>> && ...)
    void route(http::verb method, std::string_view pattern, Fn... handlers) {
        group(pattern).routes[method] = {std::move(handlers)...};
    }

    Router &group(std::string_view pattern) {
        PathCursor path(pattern);
        auto       segment = path.next_segment();

        if (!segment.has_value()) {
            return *this;
        }

        if (auto &s = *segment; s.starts_with(':')) {
            parameter_name = s;

            if (!wildcard)
                wildcard = std::make_unique<Router>();

            return wildcard->group(path.remaining());
        } else {
            auto &child = children[s];
            return child.group(path.remaining());
        }
    }
};

struct brb::Engine : Router {
    explicit Engine(asio::io_context &io)
        : acceptor(io) {}

    /// handle one stream session, returns true if keep alive
    /// @throw boost::system::system_error
    auto handle(std::shared_ptr<tcp_stream> stream, boost::optional<uint64_t> body_limit = {}) const -> awaitable<bool>;

    /// run the engine
    /// @throw runtime_error when failed to configure the listener
    struct run_opts {
        std::string         host;
        asio::ip::port_type port;
        int                 backlog              = asio::socket_base::max_listen_connections;
        std::vector<int>    cancellation_signals = {};
    };
    auto run(const run_opts &) -> awaitable<void>;

    /// close acceptor and all streams
    void close();

protected:
    auto async_work(std::shared_ptr<tcp_stream> stream) -> awaitable<void>;
    auto async_cancel(const std::vector<int> &cancellation_signals) -> awaitable<void>;

    virtual auto not_found(Context &) const -> awaitable<void>;

    asio::ip::tcp::acceptor                          acceptor;
    mutable std::mutex                               mtx;
    mutable std::vector<std::shared_ptr<tcp_stream>> streams;
};
