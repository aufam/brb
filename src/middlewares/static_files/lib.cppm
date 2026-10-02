module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

export module brb.static_files;
import brb;

export namespace brb::middlewares {

    struct static_files {
        std::string dir;
        bool        spa               = false; ///< enable SPA routes.
        bool        etag              = false; ///< enable ETag-based caching.
        bool        last_modified     = false; ///< enable last modified caching.
        bool        directory_listing = false; ///< enable directory listings.
        bool        dotfiles          = false; ///< include dotfiles in directory listings.
        bool        write_response    = false; ///< write the response immediately.
        bool        upload            = false; ///< enable upload file
        std::string cache_control     = "";    ///< set the Cache-Control response header.

        awaitable<void> operator()(Context &) const;
    };
} // namespace brb::middlewares
