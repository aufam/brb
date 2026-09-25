module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

export module brb.gzip;
import brb;

export namespace brb::gzip {
    void compress(std::string &body);
    void decompress(std::string &body);
} // namespace brb::gzip

export namespace brb::middlewares {

    /// Decompresses a gzipped request body and compresses the response
    /// when the client accepts gzip encoding.
    ///
    /// If the request body is gzip-encoded, the request parser is converted
    /// to `string_body`. Likewise, if the client accepts gzip, the response
    /// body is converted to `string_body` before compression.
    awaitable<void> gzip(Context &c);
} // namespace brb::middlewares
