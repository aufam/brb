module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <brotli/decode.h>
#include <brotli/encode.h>

module brb.brotli;

void brb::brotli::compress(std::string &body) {
    if (body.empty())
        return;

    const auto max_size = BrotliEncoderMaxCompressedSize(body.size());

    if (max_size == 0)
        throw std::runtime_error("brotli: failed to calculate compressed size");

    std::string output(max_size, '\0');
    size_t      encoded_size = max_size;

    const auto result = BrotliEncoderCompress(
        BROTLI_DEFAULT_QUALITY,
        BROTLI_DEFAULT_WINDOW,
        BROTLI_DEFAULT_MODE,
        body.size(),
        reinterpret_cast<const uint8_t *>(body.data()),
        &encoded_size,
        reinterpret_cast<uint8_t *>(output.data())
    );

    if (result != BROTLI_TRUE)
        throw std::runtime_error("brotli: compression failed");

    output.resize(encoded_size);
    body = std::move(output);
}

brb::awaitable<void> brb::middlewares::brotli(Context &c) {
    if (c.is_encoded("br")) {
        auto &parser = c.parser_string();
        if (!parser.is_done())
            co_await http::async_read(*c.stream, c.buffer, parser);

        brb::brotli::decompress(parser.get().body());
    }

    co_await c.next();

    if (c.res()[brb::http::field::content_encoding] != "")
        co_return;

    if (!c.accepts_encoding("br"))
        co_return;

    auto &res = c.response_string();

    // Avoid wasting CPU/memory for tiny responses.
    if (res.body().size() < 512)
        co_return;

    brb::brotli::compress(res.body());

    res.set(http::field::content_encoding, "br");
    res.set(http::field::vary, "Accept-Encoding");
}
