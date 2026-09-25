module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>

module brb;

std::string_view mime_type(const std::string &path) {
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

auto brb::Router::handle_file(Context &ctx, const std::string &path) const -> Handler {
    const auto  etag = etag_file(path);
    const auto &req  = ctx.req();

    if (auto it = req.find(http::field::if_none_match); it != req.end() && it->value() == etag)
        return [etag](Context &c) -> awaitable<void> {
            auto &res = c.response_empty();
            res.set(http::field::etag, etag);
            res.result(http::status::not_modified);
            res.prepare_payload();
            co_await http::async_write(*c.stream, res);
        };

    return [path, mime = mime_type(path), etag](Context &c) -> awaitable<void> {
        auto &res = c.response_file(path);
        res.set(http::field::etag, etag);
        res.set(http::field::content_type, mime);
        res.result(http::status::ok);
        res.prepare_payload();
        co_await http::async_write(*c.stream, res);
    };
}
