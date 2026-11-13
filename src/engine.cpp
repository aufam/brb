module;

#include <boost/asio.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>

module brb;

static std::string get_multipart_boundary(std::string_view);

auto brb::Engine::handle(std::shared_ptr<tcp_stream> stream, boost::optional<uint64_t> body_limit) const -> awaitable<bool> {
    Context c;
    c.stream     = stream;
    auto &parser = c.parser_empty();
    auto &req    = parser.get().base();

    parser.body_limit(body_limit);
    co_await http::async_read_header(*stream, c.buffer, parser);

    auto &res = c.response_empty();
    res.keep_alive(parser.get().keep_alive());

    const auto url = urls::parse_origin_form(req.target());
    if (!url) {
        res.result(http::status::bad_request);
        co_await http::async_write(*stream, res);
        co_return c.keep_alive();
    }

    c.url                = *url;
    c.url_path           = url->path();
    c.multipart_boundary = get_multipart_boundary(req[brb::http::field::content_type]);

    c.match(*this);
    c.handlers.push_back({"", [this](Context &c) -> awaitable<void> { co_await not_found(c); }});

    co_await c.next();
    co_await c.write_response();

    co_return c.keep_alive();
}

auto brb::Engine::run(const run_opts &opts) -> awaitable<void> {
    try {
        const auto address  = asio::ip::make_address(opts.host);
        const auto endpoint = asio::ip::tcp::endpoint(address, opts.port);

        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen(opts.backlog);
    } catch (boost::system::system_error &e) {
        std::cerr << "Failed to run server " << opts.host << ":" << opts.port << ": " << e.code().message() << std::endl;
        exit(1);
    }

    auto io = co_await asio::this_coro::executor;
    if (!opts.cancellation_signals.empty())
        asio::co_spawn(io, async_cancel(opts.cancellation_signals), asio::detached);

    std::cerr << "Server is running on http://" << opts.host << ":" << opts.port << std::endl;

    while (true) {
        auto ec     = beast::error_code();
        auto stream = std::make_shared<tcp_stream>(co_await acceptor.async_accept(asio::redirect_error(ec)));
        if (ec)
            break;

        asio::co_spawn(io, async_work(stream), asio::detached);
    }
}

bool brb::Engine::is_stopped() const {
    auto lock = std::unique_lock<std::mutex>(mtx);
    return stoppped;
}

void brb::Engine::add(std::shared_ptr<tcp_stream> stream) {
    auto lock = std::unique_lock<std::mutex>(mtx);
    streams.push_back(stream);
}

void brb::Engine::add(std::shared_ptr<ws_stream> stream) {
    auto lock = std::unique_lock<std::mutex>(mtx);
    ws_streams.push_back(stream);
}

void brb::Engine::remove(std::shared_ptr<tcp_stream> stream) {
    auto lock = std::unique_lock<std::mutex>(mtx);
    auto it   = std::remove_if(streams.begin(), streams.end(), [&](auto &s) { return s == stream; });
    streams.erase(it, streams.end());
}

void brb::Engine::remove(std::shared_ptr<ws_stream> stream) {
    auto lock = std::unique_lock<std::mutex>(mtx);
    auto it   = std::remove_if(ws_streams.begin(), ws_streams.end(), [&](auto &s) { return s == stream; });
    ws_streams.erase(it, ws_streams.end());
}

auto brb::Engine::async_work(std::shared_ptr<tcp_stream> stream) -> awaitable<void> {
    add(stream);
    defer _ = [&]() { remove(stream); };

    while (true) {
        {
            auto lock = std::unique_lock<std::mutex>(mtx);
            if (stoppped)
                break;
        }

        try {
            bool keep_alive = co_await handle(stream);
            if (!keep_alive)
                break;
        } catch (boost::system::system_error &e) {
            std::ignore = e;
            break;
        }
    }
}

auto brb::Engine::async_cancel(const std::vector<int> &cancellation_signals) -> awaitable<void> {
    asio::signal_set signals(co_await asio::this_coro::executor);
    for (int signal : cancellation_signals)
        signals.add(signal);

    std::ignore = co_await signals.async_wait();
    close();
}

void brb::Engine::close() {
    try {
        acceptor.close();
    } catch (boost::system::system_error &e) {
        std::ignore = e;
    }

    auto lock = std::unique_lock<std::mutex>(mtx);
    stoppped  = true;

    for (auto &s : streams) {
        s->close();
    }
    for (auto &s : ws_streams) {
        boost::system::error_code ec;
        s->close(brb::ws::close_code::normal, ec);
    }
}

std::string get_multipart_boundary(std::string_view content_type) {
    const auto                 value  = content_type;
    constexpr std::string_view prefix = "multipart/";

    // Must be multipart/*.
    if (value.size() < prefix.size())
        return {};

    auto media_type = value.substr(0, prefix.size());

    for (std::size_t i = 0; i < media_type.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(media_type[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return {};
    }

    // The media type may contain a subtype (e.g. `multipart/form-data`);
    // limit parsing to the parameters after the first ';'.
    const auto semicolon = value.find(';');

    if (semicolon == std::string_view::npos)
        return {};

    // Find `boundary=`.
    std::string_view parameters = value.substr(semicolon);

    while (!parameters.empty()) {
        // Skip whitespace and semicolons.
        while (!parameters.empty() &&
               (parameters.front() == ';' || std::isspace(static_cast<unsigned char>(parameters.front())))) {
            parameters.remove_prefix(1);
        }

        if (parameters.empty())
            break;

        auto equal = parameters.find('=');

        if (equal == std::string_view::npos)
            break;

        auto name = parameters.substr(0, equal);

        // Trim whitespace around parameter name.
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.remove_suffix(1);

        std::size_t name_begin = 0;

        while (name_begin < name.size() && std::isspace(static_cast<unsigned char>(name[name_begin])))
            ++name_begin;

        name.remove_prefix(name_begin);

        parameters.remove_prefix(equal + 1);

        // Find the end of this parameter.
        std::size_t end = 0;

        if (!parameters.empty() && parameters.front() == '"') {
            // Quoted value.
            parameters.remove_prefix(1);

            end = parameters.find('"');

            if (end == std::string_view::npos)
                return {};

            auto value = parameters.substr(0, end);

            if (name.size() == 8 && std::equal(name.begin(), name.end(), "boundary", [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                })) {
                return std::string(value);
            }

            parameters.remove_prefix(end + 1);
        } else {
            end = parameters.find(';');

            auto parameter = parameters.substr(0, end == std::string_view::npos ? parameters.size() : end);

            while (!parameter.empty() && std::isspace(static_cast<unsigned char>(parameter.back())))
                parameter.remove_suffix(1);

            if (name.size() == 8 && std::equal(name.begin(), name.end(), "boundary", [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                })) {
                return std::string(parameter);
            }

            if (end == std::string_view::npos)
                break;

            parameters.remove_prefix(end);
        }
    }

    return {};
}
