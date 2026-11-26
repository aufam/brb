#include <boost/asio.hpp>
#include <boost/beast.hpp>

import brb;
import brb.gzip;
import brb.static_files;
import brb.multipart;
import fmt;
import cpx;
import cpx.cli11;

struct Serve {
    std::string                directory;
    std::string                host     = "127.0.0.1";
    boost::asio::ip::port_type port     = 3000;
    uint8_t                    parallel = 1;

    static constexpr std::tuple __field_tags__ = {
        cpx::field<&Serve::directory> = "directory,positional,help=directory path to serve",
        cpx::field<&Serve::host>      = "host,short=H,skipmissing,help=address to bind the server to",
        cpx::field<&Serve::port>      = "port,short=p,skipmissing,help=port to listen on",
        cpx::field<&Serve::parallel>  = "parallel,short=j,skipmissing,help=number of worker threads",
    };
};

struct Version {
    static constexpr std::tuple __field_tags__ = {};
};

struct Args {
    std::optional<Serve>   serve;
    std::optional<Version> version;

    static constexpr std::tuple __field_tags__ = {
        cpx::field<&Args::serve>   = "serve,help=serve options",
        cpx::field<&Args::version> = "version,help=print version",
    };
};

int main(int argc, char **argv) {
    const auto args = cpx::cli11::parse<Args>("brb: Static file serving", argc, argv);
    if (args.version) {
        fmt::println("brb version {}", brb::version());
        return 0;
    }

    const auto &serve = *args.serve;

    boost::asio::io_context io;
    brb::Engine             engine{io};

    const auto logger = [](brb::Context &c) -> boost::asio::awaitable<void> {
        const auto  ep  = c.stream->socket().remote_endpoint();
        const auto &req = c.req();
        fmt::println("[{}:{}] {} {}", ep.address().to_string(), ep.port(), req.method_string(), req.target());

        try {
            co_await c.next();
        } catch (boost::system::system_error &e) {
            fmt::println("[{}:{}] {}", ep.address().to_string(), ep.port(), e.code().message());
            co_return;
        } catch (std::exception &e) {
            fmt::println("[{}:{}] {:?}", ep.address().to_string(), ep.port(), e.what());
            co_return;
        }

        const auto &res = c.res();
        fmt::println("[{}:{}] {} {}", ep.address().to_string(), ep.port(), (int)res.result(), res.reason());
    };

    const auto static_files = brb::middlewares::static_files{
        .dir               = serve.directory,
        .spa               = true,
        .directory_listing = true,
        .dotfiles          = true,
    };

    engine.use(logger, static_files);

    const auto request_reader = brb::middlewares::request_reader{
        .string_body = true,
    };

    const auto gzip = brb::middlewares::gzip{};

    auto &api = engine.group("api");

    api.use(request_reader, gzip);

    api.route(brb::http::verb::get, "/products", [](brb::Context &c) -> brb::awaitable<void> {
        auto &body = c.response_string().body();

        body = "[";
        for (int i = 0; i < 240000; i++) {
            std::string s = std::to_string(i);
            body += R"json({"id": )json" + s + ", " + R"("name": ")" + "item_" + s + R"("},)";
        }
        body.back() = ']';

        co_return;
    });

    api.route(brb::verb::post, "/echo", [](brb::Context &c) -> brb::awaitable<void> {
        c.response_string().body() = c.parser_string().get().body();
        co_return;
    });

    engine.route(brb::http::verb::get, "/stream", [](brb::Context &c) -> brb::awaitable<void> {
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

    engine.route(brb::http::verb::get, "/multipart", [](brb::Context &c) -> brb::awaitable<void> {
        const std::string boundary = "boundary";
        c.set_multipart_stream(boundary);
        brb::multipart::Part part;

        // hello
        part.headers.set(brb::http::field::content_type, "text/plain; charset=utf-8");
        part.body = "hello";
        co_await part.write(boundary, c);

        // world
        part.body = "world";
        co_await part.write(boundary, c);

        part = brb::multipart::file("atachment", "README.md");
        co_await part.write(boundary, c, false);
    });

    //
    // engine.route(brb::http::verb::post, "/multipart", [](brb::Context &c) -> brb::awaitable<void> {
    //     while (true) {
    //         fmt::println("DEBUG:2");
    //         auto part = co_await c.parse_multipart();
    //         fmt::println("DEBUG:\n{}\n\n", part.body);
    //         if (part.body.empty())
    //             break;
    //     }
    // });

    const auto run_opts = brb::Engine::run_opts{
        .host                 = serve.host,
        .port                 = serve.port,
        .cancellation_signals = {SIGINT, SIGTERM},
    };
    boost::asio::co_spawn(io, engine.run(run_opts), boost::asio::detached);

    std::vector<std::thread> ts;
    ts.reserve(serve.parallel);
    for (uint8_t i = 0; i < serve.parallel; ++i)
        ts.emplace_back([&]() { io.run(); });

    for (uint8_t i = 0; i < serve.parallel; ++i)
        ts[i].join();

    return 0;
}
