#include <boost/asio.hpp>
#include <boost/beast.hpp>

import brb;
import brb.gzip;
import brb.static_files;
import fmt;
import cpx;
import cpx.cli11;

struct Args {
    std::string                directory;
    std::string                host     = "127.0.0.1";
    boost::asio::ip::port_type port     = 3000;
    uint8_t                    parallel = 1;

    static constexpr std::tuple __field_tags__ = {
        cpx::field<&Args::directory> = "directory,positional,help=directory path to serve",
        cpx::field<&Args::host>      = "host,short=H,skipmissing,help=address to bind the server to",
        cpx::field<&Args::port>      = "port,short=p,skipmissing,help=port to listen on",
        cpx::field<&Args::parallel>  = "parallel,short=j,skipmissing,help=number of worker threads",
    };
};

int main(int argc, char **argv) {
    const auto args = cpx::cli11::parse<Args>("brb: Static file serving", argc, argv);

    boost::asio::io_context        io;
    boost::asio::ip::tcp::acceptor acceptor{io};
    brb::Router                    router;
    std::atomic_bool               is_running{true};

    static int cnt = 0;
    router.use("/", [](brb::Context &c) -> boost::asio::awaitable<void> {
        const auto  ep  = c.stream->socket().remote_endpoint();
        const auto &req = c.req();
        fmt::println("[{}:{}] {} {}", ep.address().to_string(), ep.port(), req.method_string(), req.target());

        if (cnt++ == 20)
            exit(1);

        try {
            co_await c.next();
        } catch (boost::system::system_error &e) {
            fmt::println("[{}:{}] {}", ep.address().to_string(), ep.port(), e.code().message());
            co_return;
        }

        const auto &res = c.res();
        fmt::println("[{}:{}] {} {}", ep.address().to_string(), ep.port(), (int)res.result(), res.reason());
    });

    const auto response_writer = brb::middlewares::response_writer{};

    const auto static_files = brb::middlewares::static_files{
        .dir               = args.directory,
        .spa               = true,
        .directory_listing = true,
        .dotfiles          = true,
    };

    router.use("/", response_writer, static_files);

    const auto request_reader = brb::middlewares::request_reader{
        .string_body = true,
    };

    const auto gzip = brb::middlewares::gzip{};

    router.use("/api/", request_reader, gzip);

    router.route("GET /api/products", [](brb::Context &c) -> brb::awaitable<void> {
        auto &body = c.response_string().body();

        body = "[";
        for (int i = 0; i < 240000; i++) {
            std::string s = std::to_string(i);
            body += R"json({"id": )json" + s + ", " + R"("name": ")" + "item_" + s + R"("},)";
        }
        body.back() = ']';

        co_return;
    });

    router.route("GET /stream", [](brb::Context &c) -> brb::awaitable<void> {
        c.set_event_stream();
        brb::SSE sse;

        // hello
        sse.data  = "hello";
        auto body = sse.dump();
        co_await c.write_chunk(body, true);

        // world
        sse.data = "world";
        body     = sse.dump();
        co_await c.write_chunk(body, false);
    });

    router.route("GET /multipart", [](brb::Context &c) -> brb::awaitable<void> {
        const std::string boundary = "boundary";
        c.set_multipart_stream(boundary);
        brb::Part part;

        // hello
        part.fields.set(brb::http::field::content_type, "text/plain");
        part.body = "hello";
        co_await c.write_chunk(part.dump(boundary), true);

        // world
        part.body = "world";
        co_await c.write_chunk(part.dump(boundary), true);

        co_await c.write_chunk(brb::Part::dump_end(boundary), false);
    });

    auto work = [&](std::shared_ptr<boost::beast::tcp_stream> stream) -> boost::asio::awaitable<void> {
        while (is_running) {
            bool keep_alive = co_await router.handle(stream);
            if (!keep_alive)
                break;
        }
    };

    auto async_main = [&]() -> boost::asio::awaitable<void> {
        while (is_running) {
            std::shared_ptr<boost::beast::tcp_stream> stream;
            try {
                stream = std::make_shared<boost::beast::tcp_stream>(co_await acceptor.async_accept());
            } catch (boost::system::system_error &e) {
                fmt::println("Acceptor stopped: {}", e.code().message());
                break;
            }
            boost::asio::co_spawn(io, work(stream), boost::asio::detached);
        }
    };

    auto async_cancel = [&]() -> boost::asio::awaitable<void> {
        boost::asio::signal_set signals(co_await boost::asio::this_coro::executor, SIGINT, SIGTERM);
        std::ignore = co_await signals.async_wait();

        is_running = false;
        try {
            acceptor.close();
        } catch (boost::system::system_error &e) {
            std::ignore = e;
        }

        router.close_all_streams();
    };

    try {
        const auto address  = boost::asio::ip::make_address(args.host);
        const auto endpoint = boost::asio::ip::tcp::endpoint(address, args.port);

        acceptor.open(endpoint.protocol());
        acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen(boost::asio::socket_base::max_listen_connections);
    } catch (boost::system::system_error &e) {
        fmt::println(stderr, "Failed to start server {}:{}: {}", args.host, args.port, e.code().message());
        exit(1);
    }

    boost::asio::co_spawn(io, async_main(), boost::asio::detached);
    boost::asio::co_spawn(io, async_cancel(), boost::asio::detached);

    fmt::println("Server is running on http://{}:{}", args.host, args.port);

    std::vector<std::thread> ts;
    ts.reserve(args.parallel);
    for (uint8_t i = 0; i < args.parallel; ++i)
        ts.emplace_back([&]() { io.run(); });

    for (uint8_t i = 0; i < args.parallel; ++i)
        ts[i].join();

    return 0;
}
