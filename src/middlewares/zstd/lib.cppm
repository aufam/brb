module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

export module brb.zstd;
import brb;

export namespace brb::zstd {
    void compress(std::string &body);
    void decompress(std::string &body);
} // namespace brb::zstd

export namespace brb::middlewares {

    /// Decompresses a zstd request body and compresses the response
    /// when the client accepts zstd encoding.
    ///
    /// If the request body is zstd-encoded, the request parser is converted
    /// to `string_body`. Likewise, if the client accepts zstd, the response
    /// body is converted to `string_body` before compression.
    awaitable<void> zstd(Context &c);
} // namespace brb::middlewares
