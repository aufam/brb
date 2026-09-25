module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

export module brb.brotli;
import brb;

export namespace brb::brotli {
    void compress(std::string &body);
    void decompress(std::string &body);
} // namespace brb::brotli

export namespace brb::middlewares {

    /// Decompresses a br request body and compresses the response
    /// when the client accepts br encoding.
    ///
    /// If the request body is br-encoded, the request parser is converted
    /// to `string_body`. Likewise, if the client accepts br, the response
    /// body is converted to `string_body` before compression.
    awaitable<void> brotli(Context &c);
} // namespace brb::middlewares
