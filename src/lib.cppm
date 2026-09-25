module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>
#include <map>

export module brb;

export namespace brb {
    struct Router;
    struct Context;
    struct SSE;
    struct Part;

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
    using boost::beast::http::request;
    using boost::beast::http::request_parser;
    using boost::beast::http::response;
    using boost::beast::http::response_serializer;
    using boost::beast::http::string_body;

    using URL = boost::urls::url;
} // namespace brb

export namespace brb::middlewares {
    /// read the request body and write the response body using string bodies.
    /// handles the associated HTTP I/O.
    awaitable<void> string_body(Context &);
} // namespace brb::middlewares

struct brb::Router {
    using Handler = std::function<awaitable<void>(Context &)>;

    /// register an HTTP route handler for the given path
    void route(std::string path, Handler fn) {
        std::unique_lock<std::mutex> lock(_mtx);
        handlers[std::move(path)] = std::move(fn);
    }

    /// register a middleware for the given path
    template <typename... Fn>
        requires(std::convertible_to<Fn, std::function<awaitable<void>(Context &)>> && ...)
    void use(std::string path, Fn... fn) {
        std::unique_lock lock(_mtx);
        (middlewares.insert({path, std::move(fn)}), ...);
    }

    /// mount a directory at the given path for serving files
    void mount(std::string path, std::string dir) {
        std::unique_lock<std::mutex> lock(_mtx);
        file_handlers[std::move(path)] = std::move(dir);
    }

    /// handle a TCP connection
    /// @return keep alive
    awaitable<bool> handle(std::shared_ptr<tcp_stream>, boost::optional<uint64_t> body_limit = {}) const;

    /// close currently active TCP connections
    void close_all_streams() const {
        std::unique_lock<std::mutex> lock(_mtx);
        for (auto &s : _tcp_streams)
            s->close();
    }

protected:
    using _map_handlers_t    = std::unordered_map<std::string, Handler>;
    using _map_middlewares_t = std::multimap<std::string, Handler>;

    struct _longest_first_t {
        bool operator()(const std::string &a, const std::string &b) const {
            return a.size() == b.size() ? a < b : a.size() > b.size();
        }
    };
    using _map_file_handlers_t = std::map<std::string, std::string, _longest_first_t>;

    _map_handlers_t      handlers;
    _map_file_handlers_t file_handlers;
    _map_middlewares_t   middlewares;

    mutable std::mutex                               _mtx;
    mutable std::vector<std::shared_ptr<tcp_stream>> _tcp_streams;

    void match(Context &) const;

    virtual awaitable<void> not_found(Context &c) const;
    virtual std::string     etag_file(const std::string &path) const;
    virtual Handler         handle_file(Context &, const std::string &path) const;
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
    friend Router;

    std::shared_ptr<tcp_stream> stream;
    flat_buffer                 buffer; ///< request buffer
    URL                         url;    ///< parsed url from request header
    std::string                 multipart_boundary;

    /// set local variable for this context
    template <typename T>
    void set(std::string_view key, const T &val) {
        vars[std::string(key)] = val;
    }

    /// get local variable for this context
    template <typename T>
    const T &get(std::string_view key) const {
        return std::any_cast<const T &>(vars.at(std::string(key)));
    }

    /// get local variable for this context
    template <typename T>
    T &get(std::string_view key) {
        return std::any_cast<T &>(vars.at(std::string(key)));
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
        if (idx < handlers.size())
            co_await handlers[idx++](*this);
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
    /// memory. A buffer body is copied into the string body if it contains a
    /// complete buffer.
    ///
    /// @throws std::runtime_error if the file cannot be read or the buffer body
    ///         does not contain a complete buffer.
    response<string_body> &response_string() {
        if (auto *p = std::get_if<response<empty_body>>(&r)) {
            r = response<string_body>(std::move(*p));
        } else if (auto *p = std::get_if<response<string_body>>(&r)) {
            return *p;
        } else if (auto *p = std::get_if<response<file_body>>(&r)) {
            auto res = response<string_body>(std::move(p->base()));

            if (p->body().file().is_open()) {
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
    /// @param filepath Path of the file to use for the response body.
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

    /// get request parser as string-body request
    request_parser<string_body> &parser_string() {
        if (auto *p = std::get_if<string_parser_t>(&parser))
            return *p->get();
        parser = std::make_unique<request_parser<string_body>>(std::move(parser_empty()));
        return *std::get<string_parser_t>(parser);
    }

    /// get request parser as file-body request
    request_parser<file_body> &parser_file() {
        if (auto *p = std::get_if<file_parser_t>(&parser))
            return *p->get();
        parser = std::make_unique<request_parser<file_body>>(std::move(parser_empty()));
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

    awaitable<SSE>  parse_sse();
    awaitable<Part> parse_multipart();

    /// @brief Configures the response for Server-Sent Events (SSE).
    ///
    /// Sets the content type to `text/event-stream` and enables HTTP chunked
    /// transfer encoding for streaming events.
    void set_event_stream() {
        auto &res = response_buffer();
        res.set(brb::http::field::content_type, "text/event-stream");
        res.chunked(true);
    }

    /// @brief Configures the response for a multipart stream.
    ///
    /// Sets the content type to `multipart/mixed` with the specified boundary
    /// and enables HTTP chunked transfer encoding for streaming parts.
    ///
    /// @param boundary The boundary used to separate multipart body parts.
    void set_multipart_stream(std::string_view boundary) {
        auto &res = response_buffer();
        res.set(brb::http::field::content_type, "multipart/mixed; boundary=" + std::string(boundary));
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
    /// than brb::http::error::need_buffer.
    awaitable<void> write_chunk(std::string_view data, bool more) {
        auto &res = response_buffer();
        if (!serializer) {
            serializer = std::make_unique<boost::beast::http::response_serializer<boost::beast::http::buffer_body>>(res);
        }

        res.body().data = const_cast<char *>(data.data());
        res.body().size = data.size();
        res.body().more = more;

        try {
            co_await brb::http::async_write(*stream, *serializer);
        } catch (boost::system::system_error const &e) {
            if (e.code() != brb::http::error::need_buffer)
                throw;
        }
    }

private:
    const Router &router;

    Context(const Router &router)
        : router(router) {}

    using empty_parser_t  = request_parser<empty_body>;
    using string_parser_t = std::unique_ptr<request_parser<string_body>>;
    using file_parser_t   = std::unique_ptr<request_parser<file_body>>;
    using buffer_parser_t = std::unique_ptr<request_parser<buffer_body>>;
    using serializer_t    = std::unique_ptr<response_serializer<buffer_body>>;

    std::variant<empty_parser_t, string_parser_t, file_parser_t, buffer_parser_t>                         parser;
    serializer_t                                                                                          serializer;
    std::variant<response<empty_body>, response<string_body>, response<file_body>, response<buffer_body>> r;

    std::unordered_map<std::string, std::any>              vars;
    std::vector<std::function<awaitable<void>(Context &)>> handlers;
    size_t                                                 idx = 0;

    SSE         sse;
    Part        part;
    std::string stream_buffer;

    union {
        bool part_state = false; // false = header, true = body
        bool sse_has_data;       // false = header, true = body
    };
};
