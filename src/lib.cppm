module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>
#include <map>

export module brb;

export namespace brb {
    struct Router;
    struct Context;

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

struct brb::Router {
    using Handler = std::function<awaitable<void>(Context &)>;

    /// register an HTTP route handler for the given path
    void route(std::string path, Handler fn) {
        std::unique_lock<std::mutex> lock(_mtx);
        handlers[std::move(path)] = std::move(fn);
    }

    /// register a middleware for the given path
    void use(std::string path, std::function<awaitable<void>(Context &)> fn) {
        std::unique_lock<std::mutex> lock(_mtx);
        middlewares.insert({std::move(path), std::move(fn)});
    }

    /// mount a directory at the given path for serving files
    void mount(std::string path, std::string dir) {
        std::unique_lock<std::mutex> lock(_mtx);
        file_handlers[std::move(path)] = std::move(dir);
    }

    /// handle a TCP connection
    /// @return keep alive
    awaitable<bool> handle(std::shared_ptr<tcp_stream>) const;

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

struct brb::Context {
    friend Router;

    std::shared_ptr<tcp_stream> stream;
    flat_buffer                 buffer; ///< request buffer
    URL                         url;    ///< parsed url from request header

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

    /// get response as empty-body response
    response<empty_body> &response_empty() {
        return std::get<brb::response<empty_body>>(r);
    }

    /// get the response as a string-body response.
    /// if the current response has an empty body, it is converted to a string-body response.
    response<string_body> &response_string() {
        if (auto *p = std::get_if<brb::response<string_body>>(&r))
            return *p;
        r = response<string_body>(std::move(response_empty()));
        return std::get<brb::response<string_body>>(r);
    }

    /// get the response as a file-body response.
    /// if the current response has an empty body, it is converted to a file-body response.
    response<file_body> &response_file() {
        if (auto *p = std::get_if<brb::response<file_body>>(&r))
            return *p;
        r = response<file_body>(std::move(response_empty()));
        return std::get<brb::response<file_body>>(r);
    }

    /// get the response as a buffer-body response.
    /// if the current response has an empty body, it is converted to a buffer-body response.
    response<buffer_body> &response_buffer() {
        if (auto *p = std::get_if<brb::response<buffer_body>>(&r))
            return *p;
        r = response<buffer_body>(std::move(response_empty()));
        return std::get<brb::response<buffer_body>>(r);
    }

    /// get request parser as empty-body request
    request_parser<empty_body> &parser_empty() {
        return std::get<empty_parser_t>(parser);
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

private:
    Context() = default;

    using empty_parser_t  = request_parser<empty_body>;
    using string_parser_t = std::unique_ptr<request_parser<string_body>>;
    using file_parser_t   = std::unique_ptr<request_parser<file_body>>;
    using buffer_parser_t = std::unique_ptr<request_parser<buffer_body>>;

    std::variant<empty_parser_t, string_parser_t, file_parser_t, buffer_parser_t>                         parser;
    std::variant<response<empty_body>, response<string_body>, response<file_body>, response<buffer_body>> r;

    std::unordered_map<std::string, std::any>              vars;
    std::vector<std::function<awaitable<void>(Context &)>> handlers;
    size_t                                                 idx = 0;
};
