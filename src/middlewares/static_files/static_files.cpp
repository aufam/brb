module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>
#include <filesystem>
#include <fstream>
#include <xxhash.h>

module brb.static_files;
import brb.date;

namespace fs = std::filesystem;

namespace {
    struct MatchResult {
        std::string path     = {};
        bool        redirect = false;

        std::vector<std::string> directory_entries = {};
        bool                     is_dir            = false;
    };
} // namespace

static MatchResult match_filepath(std::string_view root_uri, const std::string &uri, const brb::middlewares::static_files &opts) {
    if (!uri.starts_with(root_uri))
        return {};

    if (uri.size() > root_uri.size() && !root_uri.ends_with('/') && uri[root_uri.size()] != '/')
        return {};

    std::error_code ec;
    const auto      root = fs::weakly_canonical(opts.dir, ec);
    if (ec)
        return {};

    auto path = fs::weakly_canonical(root / uri.substr(root_uri.size()), ec);
    if (ec)
        return {};

    if (auto rel = path.lexically_relative(root); rel.empty() || rel == ".." || rel.native().starts_with("../"))
        return {};

    const bool is_dir = fs::is_directory(path, ec);
    if (!ec && is_dir && !uri.ends_with('/')) {
        return {.path = uri + '/', .redirect = true}; // redirect to uri + /
    }

    auto temp = path;
    for (int i = 0; i < 2; ++i) {
        std::error_code ec;
        const bool      exists = fs::is_regular_file(path, ec);
        if (!ec && exists)
            return {.path = path.string(), .redirect = false};

        if (!opts.spa)
            break;

        if (is_dir)
            path /= "index.html";
        else
            path += ".html";
    }
    path = temp;

    if (!ec && is_dir) {
        std::vector<std::string> entries;

        for (const auto &entry : fs::directory_iterator(path, ec)) {
            if (ec)
                return {};

            const auto name = entry.path().filename().string();

            if (!opts.dotfiles && name.starts_with('.'))
                continue;

            entries.emplace_back(name);
        }

        if (ec)
            return {};

        std::ranges::sort(entries);

        return {
            .path              = path.string(),
            .redirect          = false,
            .directory_entries = std::move(entries),
            .is_dir            = true,
        };
    }

    return {};
}

static std::string_view mime_type(const std::string &path) {
    const auto ext = std::filesystem::path(path).extension().string();

    static const std::unordered_map<std::string_view, std::string_view> mime{
        // Web
        {".html",  "text/html"                                                                },
        {".htm",   "text/html"                                                                },
        {".css",   "text/css"                                                                 },
        {".js",    "text/javascript"                                                          },
        {".mjs",   "text/javascript"                                                          },
        {".json",  "application/json"                                                         },
        {".map",   "application/json"                                                         },
        {".xml",   "application/xml"                                                          },
        {".csv",   "text/csv"                                                                 },
        {".txt",   "text/plain"                                                               },
        {".md",    "text/markdown"                                                            },
        {".toml",  "application/toml"                                                         },
        {".yaml",  "application/yaml"                                                         },
        {".yml",   "application/yaml"                                                         },

        // Images
        {".png",   "image/png"                                                                },
        {".jpg",   "image/jpeg"                                                               },
        {".jpeg",  "image/jpeg"                                                               },
        {".gif",   "image/gif"                                                                },
        {".webp",  "image/webp"                                                               },
        {".avif",  "image/avif"                                                               },
        {".svg",   "image/svg+xml"                                                            },
        {".ico",   "image/vnd.microsoft.icon"                                                 },
        {".bmp",   "image/bmp"                                                                },
        {".tif",   "image/tiff"                                                               },
        {".tiff",  "image/tiff"                                                               },

        // Audio
        {".mp3",   "audio/mpeg"                                                               },
        {".wav",   "audio/wav"                                                                },
        {".ogg",   "audio/ogg"                                                                },
        {".oga",   "audio/ogg"                                                                },
        {".opus",  "audio/opus"                                                               },
        {".m4a",   "audio/mp4"                                                                },
        {".aac",   "audio/aac"                                                                },
        {".flac",  "audio/flac"                                                               },

        // Video
        {".mp4",   "video/mp4"                                                                },
        {".webm",  "video/webm"                                                               },
        {".ogv",   "video/ogg"                                                                },
        {".mov",   "video/quicktime"                                                          },
        {".m4v",   "video/mp4"                                                                },

        // Fonts
        {".woff",  "font/woff"                                                                },
        {".woff2", "font/woff2"                                                               },
        {".ttf",   "font/ttf"                                                                 },
        {".otf",   "font/otf"                                                                 },

        // Documents
        {".pdf",   "application/pdf"                                                          },
        {".rtf",   "application/rtf"                                                          },
        {".doc",   "application/msword"                                                       },
        {".docx",  "application/vnd.openxmlformats-officedocument.wordprocessingml.document"  },
        {".xls",   "application/vnd.ms-excel"                                                 },
        {".xlsx",  "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"        },
        {".ppt",   "application/vnd.ms-powerpoint"                                            },
        {".pptx",  "application/vnd.openxmlformats-officedocument.presentationml.presentation"},

        // Archives / packages
        {".zip",   "application/zip"                                                          },
        {".gz",    "application/gzip"                                                         },
        {".tar",   "application/x-tar"                                                        },
        {".7z",    "application/x-7z-compressed"                                              },
        {".rar",   "application/vnd.rar"                                                      },
        {".bz2",   "application/x-bzip2"                                                      },
        {".xz",    "application/x-xz"                                                         },
        {".zst",   "application/zstd"                                                         },

        // WebAssembly
        {".wasm",  "application/wasm"                                                         },

        // Binary
        {".bin",   "application/octet-stream"                                                 },
    };

    if (auto it = mime.find(ext); it != mime.end())
        return it->second;

    return "application/octet-stream";
}

/// Convert a filesystem timestamp to system_clock.
static std::chrono::system_clock::time_point to_system_clock(std::filesystem::file_time_type tp) {
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        tp - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
}

static std::string etag_file(std::string const &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};

    XXH3_state_t *state = XXH3_createState();
    XXH3_128bits_reset(state);

    std::array<char, 8192> buf;
    while (f.read(buf.data(), buf.size()) || f.gcount())
        XXH3_128bits_update(state, buf.data(), f.gcount());

    auto hash = XXH3_128bits_digest(state);
    XXH3_freeState(state);

    char etag[33];
    snprintf(etag, sizeof(etag), "%016llx%016llx", (unsigned long long)hash.high64, (unsigned long long)hash.low64);

    return std::string("\"") + etag + "\"";
}

auto brb::middlewares::static_files::operator()(Context &c) const -> awaitable<void> {
    auto &opts = *this;

    if (c.req().method() != http::verb::get) {
        co_await c.next();
        co_return;
    }

    auto [path, redirect, entries, is_dir] = match_filepath(c.matched_path, c.url_path, opts);
    if ((path.empty() && !is_dir) || (is_dir && !opts.directory_listing)) {
        co_await c.next();
        co_return;
    }

    if (is_dir) {
        std::string body;
        std::string url_path = std::string(c.url_path);

        body += "<!doctype html><html><head><meta charset=\"utf-8\">";
        body += "<title>Index of " + url_path + "</title>";
        body += "</head><body>";
        body += "<h1>Index of " + url_path + "</h1>";
        body += "<ul>";

        for (const auto &entry : entries) {
            body += "<li><a href=\"";
            body += boost::urls::encode(entry, boost::urls::pchars);
            body += "\">";
            body += entry;
            body += "</a></li>";
        }

        body += "</ul>";
        body += "</body></html>";

        auto &res  = c.response_string();
        res.body() = std::move(body);
        res.set(http::field::content_type, "text/html; charset=utf-8");
        res.result(http::status::ok);

        co_await c.write_response();
        co_return;
    }

    if (!cache_control.empty()) {
        c.res().set(http::field::cache_control, cache_control);
    }

    if (redirect) {
        auto uri   = boost::urls::url{};
        auto query = c.url.encoded_query();

        uri.set_path(path);
        if (!query.empty())
            uri.set_encoded_query(query);

        auto &res = c.response_empty();
        res.set(http::field::location, uri.buffer());
        res.result(http::status::moved_permanently);

        co_await c.write_response();
        co_return;
    }

    std::string etag;
    if (opts.etag) {
        etag = etag_file(path);

        const auto &req = c.req();
        if (auto it = req.find(http::field::if_none_match); it != req.end() && it->value() == etag) {
            auto &res = c.response_empty();
            res.set(http::field::etag, etag);
            res.result(http::status::not_modified);
            co_await c.write_response();
            co_return;
        }
    }

    std::optional<std::string> last_modified;
    if (opts.last_modified) {
        std::error_code ec;
        const auto      file_time = std::filesystem::last_write_time(path, ec);

        if (!ec) {
            const auto modified = std::chrono::time_point_cast<std::chrono::seconds>(to_system_clock(file_time));

            last_modified = date::format(modified);

            const auto &req = c.req();
            if (auto it = req.find(http::field::if_modified_since); it != req.end()) {

                auto since = date::parse(it->value());
                if (modified <= std::chrono::time_point_cast<std::chrono::seconds>(since)) {

                    auto &res = c.response_empty();

                    if (!etag.empty())
                        res.set(http::field::etag, etag);

                    res.set(http::field::last_modified, *last_modified);
                    res.result(http::status::not_modified);
                    co_await c.write_response();
                    co_return;
                }
            }
        }
    }

    auto &res = c.response_file(path);

    if (!etag.empty())
        res.set(http::field::etag, etag);

    if (last_modified)
        res.set(http::field::last_modified, *last_modified);

    res.set(http::field::content_type, mime_type(path));
    res.result(http::status::ok);
    res.prepare_payload();
    co_await c.write_response();
}
