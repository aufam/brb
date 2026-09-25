module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <zstd.h>

module brb.zstd;


void brb::zstd::compress(std::string &body) {
    if (body.empty())
        return;

    const auto bound = ZSTD_compressBound(body.size());

    std::string output(bound, '\0');

    const auto size = ZSTD_compress(output.data(), output.size(), body.data(), body.size(), ZSTD_CLEVEL_DEFAULT);

    if (ZSTD_isError(size))
        throw std::runtime_error(std::string("zstd: compression failed: ") + ZSTD_getErrorName(size));

    output.resize(size);
    body = std::move(output);
}

void brb::zstd::decompress(std::string &body) {
    if (body.empty())
        return;

    // For a single-shot ZSTD_decompress(), the decompressed size must
    // be known beforehand. Prefer the content size embedded in the frame.
    const auto frame_size = ZSTD_getFrameContentSize(body.data(), body.size());

    if (frame_size == ZSTD_CONTENTSIZE_ERROR)
        throw std::runtime_error("zstd: invalid compressed frame");

    if (frame_size == ZSTD_CONTENTSIZE_UNKNOWN)
        throw std::runtime_error("zstd: decompressed size is unknown");

    if (frame_size > static_cast<unsigned long long>(std::numeric_limits<size_t>::max()))
        throw std::runtime_error("zstd: decompressed size is too large");

    std::string output(static_cast<size_t>(frame_size), '\0');

    const auto size = ZSTD_decompress(output.data(), output.size(), body.data(), body.size());

    if (ZSTD_isError(size))
        throw std::runtime_error(std::string("zstd: decompression failed: ") + ZSTD_getErrorName(size));

    output.resize(size);
    body = std::move(output);
}

brb::awaitable<void> brb::middlewares::zstd(Context &c) {
    if (c.is_encoded("zstd")) {
        auto &parser = c.parser_string();
        if (!parser.is_done())
            co_await http::async_read(*c.stream, c.buffer, parser);

        brb::zstd::decompress(parser.get().body());
    }

    co_await c.next();

    if (c.res()[brb::http::field::content_encoding] != "")
        co_return;

    if (!c.accepts_encoding("zstd"))
        co_return;

    auto &res = c.response_string();

    // Avoid wasting CPU/memory for tiny responses.
    if (res.body().size() < 512)
        co_return;

    brb::zstd::compress(res.body());

    res.set(http::field::content_encoding, "zstd");
    res.set(http::field::vary, "Accept-Encoding");
}
