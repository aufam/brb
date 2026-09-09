#include <boost/asio.hpp>
#include <boost/beast.hpp>

import brb;
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

    router.mount("/", args.directory);

    router.use("/", [](brb::Context &c) -> boost::asio::awaitable<void> {
        const auto  ep  = c.stream->socket().remote_endpoint();
        const auto &req = c.req();
        fmt::println("[{}:{}] {} {}", ep.address().to_string(), ep.port(), req.method_string(), req.target());

        try {
            co_await c.next();
        } catch (boost::system::system_error &e) {
            fmt::println("[{}:{}] {}", ep.address().to_string(), ep.port(), e.code().message());
            co_return;
        }

        const auto &res = c.res();
        fmt::println("[{}:{}] {} {}", ep.address().to_string(), ep.port(), (int)res.result(), res.reason());
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
