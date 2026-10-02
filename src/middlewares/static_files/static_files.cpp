module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/url.hpp>
#include <filesystem>
#include <fstream>
#include <xxhash.h>

module brb.static_files;
namespace fs = std::filesystem;

namespace {
    struct MatchResult {
        std::string path     = {};
        bool        redirect = false;

        std::vector<std::string> directory_entries = {};
        bool                     is_dir            = false;
    };
} // namespace

static MatchResult
match_filepath(const std::string &root_uri, const std::string &uri, const brb::middlewares::static_files &opts) {
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


/// Parse an HTTP-date in IMF-fixdate form:
/// `Sun, 06 Nov 1994 08:49:37 GMT`.
///
/// Returns an empty optional for an invalid date.
static std::optional<std::chrono::system_clock::time_point> parse_http_date(std::string_view value) {
    // IMF-fixdate is exactly:
    //   wkday, SP DD SP month SP YYYY SP HH ":" MM ":" SS SP GMT
    if (value.size() != 29 || value[3] != ',' || value[4] != ' ' || value[7] != ' ' || value[11] != ' ' || value[16] != ' ' ||
        value[19] != ':' || value[22] != ':' || value[25] != ' ' || value.substr(26) != "GMT") {
        return std::nullopt;
    }

    constexpr std::array<std::string_view, 12> months = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    auto digit = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : -1; };

    auto number = [&](std::size_t pos, std::size_t count) -> int {
        int result = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto d = digit(value[pos + i]);
            if (d < 0)
                return -1;
            result = result * 10 + d;
        }
        return result;
    };

    // Validate weekday.
    constexpr std::array<std::string_view, 7> weekdays = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    bool valid_weekday = false;
    for (auto weekday : weekdays) {
        if (value.substr(0, 3) == weekday) {
            valid_weekday = true;
            break;
        }
    }

    if (!valid_weekday)
        return std::nullopt;

    const int day    = number(5, 2);
    const int year   = number(12, 4);
    const int hour   = number(17, 2);
    const int minute = number(20, 2);
    const int second = number(23, 2);

    if (day < 1 || day > 31 || year < 1601 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) {
        return std::nullopt;
    }

    int month = 0;
    for (std::size_t i = 0; i < months.size(); ++i) {
        if (value.substr(8, 3) == months[i]) {
            month = static_cast<int>(i) + 1;
            break;
        }
    }

    if (month == 0)
        return std::nullopt;

    // Howard Hinnant's civil-date -> days algorithm.
    // Produces days relative to 1970-01-01 without involving the
    // local timezone.
    const auto days_from_civil = [](int y, unsigned m, unsigned d) {
        y -= m <= 2;
        const int      era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

        return era * 146097 + static_cast<int>(doe) - 719468;
    };

    // Basic calendar validation.
    const int days = days_from_civil(year, month, day);

    // Check that converting the date didn't normalize an invalid
    // day/month combination.
    if (days_from_civil(year, month, day) != days)
        return std::nullopt;

    return std::chrono::system_clock::time_point{
        std::chrono::seconds{static_cast<std::int64_t>(days) * 86400 + hour * 3600 + minute * 60 + second}
    };
}

static std::string format_http_date(std::chrono::system_clock::time_point tp) {
    const auto time     = std::chrono::time_point_cast<std::chrono::seconds>(tp);
    const auto duration = time.time_since_epoch();
    const auto seconds  = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

    // Convert Unix timestamp to civil date.
    const auto days        = seconds / 86400;
    const auto day_seconds = seconds % 86400;

    // Howard Hinnant's civil_from_days algorithm.
    auto civil_from_days = [](std::int64_t z) {
        z += 719468;

        const auto era = (z >= 0 ? z : z - 146096) / 146097;
        const auto doe = static_cast<unsigned>(z - era * 146097);
        const auto yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;

        auto       y   = static_cast<std::int64_t>(yoe) + era * 400;
        const auto doy = doe - (365 * yoe + yoe / 4 - yoe / 100);

        const auto mp = (5 * doy + 2) / 153;
        const auto d  = doy - (153 * mp + 2) / 5 + 1;
        const auto m  = mp + (mp < 10 ? 3 : -9);

        y += m <= 2;

        return std::array<std::int64_t, 3>{y, m, d};
    };

    const auto date = civil_from_days(days);

    // days since 1970-01-01; 1970-01-01 was Thursday.
    const auto weekday = static_cast<unsigned>((days % 7 + 4 + 7) % 7);

    constexpr std::array<std::string_view, 7> weekdays = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};

    constexpr std::array<std::string_view, 12> months = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    const auto hour   = static_cast<unsigned>(day_seconds / 3600);
    const auto minute = static_cast<unsigned>((day_seconds % 3600) / 60);
    const auto second = static_cast<unsigned>(day_seconds % 60);

    std::string result;
    result.reserve(29);

    result += weekdays[weekday];
    result += ", ";

    auto append_2 = [&](unsigned value) {
        result += static_cast<char>('0' + value / 10);
        result += static_cast<char>('0' + value % 10);
    };

    auto append_4 = [&](std::int64_t value) {
        result += static_cast<char>('0' + (value / 1000) % 10);
        result += static_cast<char>('0' + (value / 100) % 10);
        result += static_cast<char>('0' + (value / 10) % 10);
        result += static_cast<char>('0' + value % 10);
    };

    append_2(static_cast<unsigned>(date[2]));
    result += ' ';
    result += months[static_cast<unsigned>(date[1]) - 1];
    result += ' ';
    append_4(date[0]);
    result += ' ';
    append_2(hour);
    result += ':';
    append_2(minute);
    result += ':';
    append_2(second);
    result += " GMT";

    return result;
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

    auto [path, redirect, entries, is_dir] = match_filepath(c.current_middleware_path(), c.url.path(), opts);
    if (path.empty() && !is_dir) {
        co_await c.next();
        co_return;
    }

    if (is_dir) {
        std::string body;
        body += "<!doctype html><html><head><meta charset=\"utf-8\">";
        body += "<title>Index of " + c.url.path() + "</title>";
        body += "</head><body>";
        body += "<h1>Index of " + c.url.path() + "</h1>";
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
        auto  query = std::string(c.url.encoded_query());
        auto &res   = c.response_empty();
        auto  uri   = path + (query.empty() ? "" : "?") + query;
        res.result(http::status::moved_permanently);
        res.set(http::field::location, uri);
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

            last_modified = format_http_date(modified);

            const auto &req = c.req();
            if (auto it = req.find(http::field::if_modified_since); it != req.end()) {

                if (auto since = parse_http_date(it->value());
                    since && modified <= std::chrono::time_point_cast<std::chrono::seconds>(*since)) {

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
