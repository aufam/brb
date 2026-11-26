module;

#include <string_view>

module brb;

namespace {
    struct MimeMapping {
        std::string_view extension;
        std::string_view mime_type;
    };

    // The first extension for a MIME type is its preferred extension.
    constexpr MimeMapping mappings[] = {
        // Application
        {"7z",     "application/x-7z-compressed"                                              },
        {"abw",    "application/x-abiword"                                                    },
        {"arc",    "application/x-freearc"                                                    },
        {"azw",    "application/vnd.amazon.ebook"                                             },
        {"bin",    "application/octet-stream"                                                 },
        {"bz",     "application/x-bzip"                                                       },
        {"bz2",    "application/x-bzip2"                                                      },
        {"cda",    "application/x-cdf"                                                        },
        {"doc",    "application/msword"                                                       },
        {"docx",   "application/vnd.openxmlformats-officedocument.wordprocessingml.document"  },
        {"epub",   "application/epub+zip"                                                     },
        {"gz",     "application/gzip"                                                         },
        {"json",   "application/json"                                                         },
        {"jsonld", "application/ld+json"                                                      },
        {"pdf",    "application/pdf"                                                          },
        {"ppt",    "application/vnd.ms-powerpoint"                                            },
        {"pptx",   "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {"rar",    "application/vnd.rar"                                                      },
        {"rtf",    "application/rtf"                                                          },
        {"sh",     "application/x-sh"                                                         },
        {"tar",    "application/x-tar"                                                        },
        {"wasm",   "application/wasm"                                                         },
        {"xls",    "application/vnd.ms-excel"                                                 },
        {"xlsx",   "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"        },
        {"xml",    "application/xml"                                                          },
        {"yaml",   "application/yaml"                                                         },
        {"yml",    "application/yaml"                                                         },
        {"zip",    "application/zip"                                                          },

        // Audio
        {"aac",    "audio/aac"                                                                },
        {"flac",   "audio/flac"                                                               },
        {"m4a",    "audio/mp4"                                                                },
        {"mid",    "audio/midi"                                                               },
        {"midi",   "audio/midi"                                                               },
        {"mp3",    "audio/mpeg"                                                               },
        {"ogg",    "audio/ogg"                                                                },
        {"oga",    "audio/ogg"                                                                },
        {"opus",   "audio/ogg"                                                                },
        {"wav",    "audio/wav"                                                                },
        {"weba",   "audio/webm"                                                               },

        // Font
        {"eot",    "application/vnd.ms-fontobject"                                            },
        {"otf",    "font/otf"                                                                 },
        {"ttf",    "font/ttf"                                                                 },
        {"woff",   "font/woff"                                                                },
        {"woff2",  "font/woff2"                                                               },

        // Image
        {"apng",   "image/apng"                                                               },
        {"avif",   "image/avif"                                                               },
        {"bmp",    "image/bmp"                                                                },
        {"gif",    "image/gif"                                                                },
        {"heic",   "image/heic"                                                               },
        {"heif",   "image/heif"                                                               },
        {"ico",    "image/vnd.microsoft.icon"                                                 },
        {"jpg",    "image/jpeg"                                                               },
        {"jpeg",   "image/jpeg"                                                               },
        {"jpe",    "image/jpeg"                                                               },
        {"png",    "image/png"                                                                },
        {"svg",    "image/svg+xml"                                                            },
        {"tif",    "image/tiff"                                                               },
        {"tiff",   "image/tiff"                                                               },
        {"webp",   "image/webp"                                                               },

        // Model / 3D
        {"3mf",    "model/3mf"                                                                },
        {"gltf",   "model/gltf+json"                                                          },
        {"glb",    "model/gltf-binary"                                                        },
        {"obj",    "model/obj"                                                                },
        {"stl",    "model/stl"                                                                },

        // Text
        {"css",    "text/css"                                                                 },
        {"csv",    "text/csv"                                                                 },
        {"html",   "text/html"                                                                },
        {"htm",    "text/html"                                                                },
        {"ics",    "text/calendar"                                                            },
        {"js",     "text/javascript"                                                          },
        {"mjs",    "text/javascript"                                                          },
        {"md",     "text/markdown"                                                            },
        {"txt",    "text/plain"                                                               },

        // Video
        {"3gp",    "video/3gpp"                                                               },
        {"3g2",    "video/3gpp2"                                                              },
        {"avi",    "video/x-msvideo"                                                          },
        {"m4v",    "video/x-m4v"                                                              },
        {"mkv",    "video/x-matroska"                                                         },
        {"mov",    "video/quicktime"                                                          },
        {"mp4",    "video/mp4"                                                                },
        {"mpeg",   "video/mpeg"                                                               },
        {"mpg",    "video/mpeg"                                                               },
        {"ogv",    "video/ogg"                                                                },
        {"ts",     "video/mp2t"                                                               },
        {"webm",   "video/webm"                                                               },
    };
} // namespace

std::string_view brb::mime_type(std::string_view extension) {
    extension = trim(extension);

    if (!extension.empty() && extension.front() == '.')
        extension.remove_prefix(1);

    for (auto const &mapping : mappings) {
        if (iequals(extension, mapping.extension))
            return mapping.mime_type;
    }

    return "application/octet-stream";
}

std::string_view brb::file_extension(std::string_view mime_type) {
    mime_type = trim(mime_type);

    // Ignore MIME parameters, e.g. "; charset=utf-8".
    if (auto pos = mime_type.find(';'); pos != std::string_view::npos)
        mime_type = trim(mime_type.substr(0, pos));

    for (auto const &mapping : mappings) {
        if (iequals(mime_type, mapping.mime_type)) {
            // Return the preferred extension, including the dot.
            return mapping.extension;
        }
    }

    return "bin";
}
