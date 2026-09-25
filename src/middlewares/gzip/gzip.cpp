module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <zlib.h>

module brb.gzip;

void brb::gzip::compress(std::string &body) {
    if (body.empty())
        return;

    z_stream zs{};

    const int ret = deflateInit2(
        &zs,
        Z_DEFAULT_COMPRESSION,
        Z_DEFLATED,
        15 + 16, // gzip wrapper
        8,
        Z_DEFAULT_STRATEGY
    );

    if (ret != Z_OK)
        throw std::runtime_error("gzip: deflateInit2 failed");

    // Ask zlib for a safe upper bound.
    std::string compressed;
    compressed.resize(deflateBound(&zs, static_cast<uLong>(body.size())));

    zs.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(body.data()));
    zs.avail_in = static_cast<uInt>(body.size());

    zs.next_out  = reinterpret_cast<Bytef *>(compressed.data());
    zs.avail_out = static_cast<uInt>(compressed.size());

    const int result = deflate(&zs, Z_FINISH);

    if (result != Z_STREAM_END) {
        deflateEnd(&zs);
        throw std::runtime_error("gzip: deflate failed");
    }

    compressed.resize(zs.total_out);

    deflateEnd(&zs);

    body = std::move(compressed);
}

void brb::gzip::decompress(std::string &body) {
    if (body.empty())
        return;

    z_stream zs{};

    const int ret = inflateInit2(&zs, 15 + 16); // gzip wrapper

    if (ret != Z_OK)
        throw std::runtime_error("gzip: inflateInit2 failed");

    zs.next_in  = reinterpret_cast<Bytef *>(body.data());
    zs.avail_in = static_cast<uInt>(body.size());

    std::string decompressed;
    char        buffer[8192];

    int result;
    do {
        zs.next_out  = reinterpret_cast<Bytef *>(buffer);
        zs.avail_out = sizeof(buffer);

        result = inflate(&zs, Z_NO_FLUSH);

        if (result != Z_OK && result != Z_STREAM_END) {
            inflateEnd(&zs);
            throw std::runtime_error("gzip: inflate failed");
        }

        decompressed.append(buffer, sizeof(buffer) - zs.avail_out);
    } while (result != Z_STREAM_END);

    inflateEnd(&zs);

    body = std::move(decompressed);
}

auto brb::middlewares::gzip(Context &c) -> awaitable<void> {
    if (c.is_encoded("gzip")) {
        auto &parser = c.parser_string();
        if (!parser.is_done())
            co_await http::async_read(*c.stream, c.buffer, parser);

        brb::gzip::decompress(parser.get().body());
    }

    co_await c.next();

    if (c.res()[brb::http::field::content_encoding] != "")
        co_return;

    if (!c.accepts_encoding("gzip"))
        co_return;

    auto &res = c.response_string();

    // Avoid wasting CPU/memory for tiny responses.
    if (res.body().size() < 512)
        co_return;

    brb::gzip::compress(res.body());

    res.set(http::field::content_encoding, "gzip");
    res.set(http::field::vary, "Accept-Encoding");
}
