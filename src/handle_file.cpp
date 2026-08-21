module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>

module brb;

std::string_view mime_type(const std::string &path) {
    const auto ext = std::filesystem::path(path).extension().string();

    static const std::unordered_map<std::string, std::string_view> mime{
        {".html", "text/html"             },
        {".css",  "text/css"              },
        {".js",   "application/javascript"},
        {".json", "application/json"      },
        {".png",  "image/png"             },
        {".jpg",  "image/jpeg"            },
        {".jpeg", "image/jpeg"            },
        {".svg",  "image/svg+xml"         },
        {".ico",  "image/x-icon"          },
        {".wasm", "application/wasm"      },
    };

    if (auto it = mime.find(ext); it != mime.end())
        return it->second;

    return "application/octet-stream";
}

auto brb::Router::handle_file(Context &ctx, const std::string &path) const -> Handler {
    const auto  etag = etag_file(path);
    const auto &req  = ctx.req();

    if (auto it = ctx.req().find(http::field::if_none_match); it != req.end() && it->value() == etag)
        return [etag](Context &c) -> awaitable<void> {
            auto &res = c.response_empty();
            res.set(http::field::etag, etag);
            res.set(http::field::cache_control, "no-cache");
            res.result(http::status::not_modified);
            res.prepare_payload();
            co_await http::async_write(*c.stream, res);
        };

    return [=, mime = mime_type(path)](Context &c) -> awaitable<void> {
        auto &res = c.response_file();
        auto  ec  = beast::error_code();
        res.body().open(path.c_str(), beast::file_mode::scan, ec);
        if (ec)
            throw boost::system::system_error(ec);

        res.set(http::field::etag, etag);
        res.set(http::field::cache_control, "no-cache");
        res.result(http::status::ok);
        res.prepare_payload();
        co_await http::async_write(*c.stream, res);
    };
}
