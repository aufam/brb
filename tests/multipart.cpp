#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>

import brb;
import brb.multipart;


TEST(multipart, form_data) {
    brb::asio::io_context io;

    brb::tcp::acceptor acceptor{
        io, brb::tcp::endpoint{brb::asio::ip::address_v4::loopback(), 0}
    };

    auto client = std::make_shared<brb::tcp_stream>(io);
    client->connect(acceptor.local_endpoint());

    auto server = std::make_shared<brb::tcp_stream>(io);
    acceptor.accept(server->socket());

    brb::Engine r(io);
    r.route(brb::verb::post, "/multipart", [](brb::Context &c) -> brb::awaitable<void> {
        brb::multipart::Parser parser(c);

        EXPECT_FALSE(parser.done());

        auto [part, err] = co_await parser.get();

        EXPECT_EQ(err, "");
        EXPECT_EQ(part.body, "Hello world");
        EXPECT_TRUE(parser.done());
    });

    auto handle = [](std::shared_ptr<brb::tcp_stream> client) -> brb::awaitable<void> {
        brb::request<brb::buffer_body> req;
        req.method(brb::verb::post);
        req.target("/multipart");
        req.version(11);
        req.set(brb::field::content_type, "multipart/form-data; boundary=boundary");
        req.chunked(true);
        req.prepare_payload();

        brb::request_serializer<brb::buffer_body> sr(req);
        co_await brb::async_write_header(*client, sr);

        brb::multipart::Part p;
        p.headers.set(brb::field::content_disposition, "form-data; name=\"note\"; filename=\"note.md\"");
        p.headers.set(brb::field::content_type, "text/plain");
        p.body = "Hello world";

        std::string payload = p.dump("boundary");
        req.body().data     = payload.data();
        req.body().size     = payload.size();
        req.body().more     = true;

        boost::system::error_code ec;
        co_await brb::async_write_some(*client, sr, brb::asio::redirect_error(ec));
        if (ec && ec != brb::http::error::need_more && ec != brb::http::error::need_buffer)
            throw boost::system::system_error(ec);

        /// after the previous write the buffer_body serializer demands a refill
        /// (need_buffer) before it accepts the next data buffer
        co_await brb::async_write_some(*client, sr, brb::asio::redirect_error(ec));
        if (ec != brb::http::error::need_buffer)
            throw boost::system::system_error(ec);

        payload         = p.dump_end("boundary");
        req.body().data = payload.data();
        req.body().size = payload.size();
        req.body().more = false;
        co_await brb::async_write_some(*client, sr, brb::asio::redirect_error(ec));
        if (ec)
            throw boost::system::system_error(ec);
    };

    brb::asio::co_spawn(io, handle(client), brb::asio::detached);
    brb::asio::co_spawn(io, r.handle(server), brb::asio::detached);
    io.run();
}
