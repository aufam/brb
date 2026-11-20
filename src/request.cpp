module;

#include <boost/beast.hpp>
#include <boost/asio.hpp>

module brb;

auto brb::Context::req() const -> const request_header & {
    if (parser.index() == 0) {
        return std::get<0>(parser).get().base();
    } else if (parser.index() == 1) {
        return std::get<1>(parser)->get().base();
    } else if (parser.index() == 2) {
        return std::get<2>(parser)->get().base();
    } else {
        return std::get<3>(parser)->get().base();
    }
}

auto brb::Context::parser_empty() -> request_parser<empty_body> & {
    if (auto *p = std::get_if<empty_parser_t>(&parser)) {
        return *p;
    } else if (std::get_if<string_parser_t>(&parser)) {
        throw std::runtime_error("the parser is already converted to string parser");
    } else if (std::get_if<file_parser_t>(&parser)) {
        throw std::runtime_error("the parser is already converted to file parser");
    } else if (std::get_if<buffer_parser_t>(&parser)) {
        throw std::runtime_error("the parser is already converted to buffer parser");
    } else {
        throw std::runtime_error("unknown parser");
    }
}

auto brb::Context::parser_string() -> request_parser<string_body> & {
    if (auto *p = std::get_if<empty_parser_t>(&parser)) {
        parser = std::make_unique<request_parser<string_body>>(std::move(*p));
    } else if (auto *p = std::get_if<string_parser_t>(&parser)) {
        return *p->get();
    } else if (auto *p = std::get_if<file_parser_t>(&parser)) {
        auto &fp = *p->get();
        if (!fp.is_done())
            throw std::runtime_error("the parser is already converted to file parser and it's not done");

        std::string body;
        body.resize(fp.get().body().size());

        auto ec = beast::error_code();
        fp.get().body().file().read(body.data(), body.size(), ec);

        if (ec)
            throw std::runtime_error(ec.message());

        auto sp          = std::make_unique<request_parser<string_body>>(std::move(fp.get().base()));
        sp->get().body() = std::move(body);
        parser           = std::move(sp);
    } else if (std::get_if<buffer_parser_t>(&parser)) {
        throw std::runtime_error("the parser is already converted to buffer parser");
    } else {
        throw std::runtime_error("unknown parser");
    }

    return *std::get<string_parser_t>(parser);
}

auto brb::Context::parser_file(const std::string filepath) -> request_parser<file_body> & {
    if (auto *p = std::get_if<empty_parser_t>(&parser)) {
        auto fp = std::make_unique<request_parser<file_body>>(std::move(*p));

        if (!filepath.empty()) {
            auto ec = beast::error_code();
            fp->get().body().open(filepath.c_str(), beast::file_mode::write, ec);

            if (ec)
                throw std::runtime_error(ec.message());
        }

        parser = std::move(fp);
    } else if (std::get_if<string_parser_t>(&parser)) {
        throw std::runtime_error("the parser is already converted to string parser");
    } else if (auto *p = std::get_if<file_parser_t>(&parser)) {
        auto &fp = *p->get();
        if (filepath.empty())
            return fp;

        auto ec = beast::error_code();
        fp.get().body().open(filepath.c_str(), beast::file_mode::write, ec);

        if (ec)
            throw std::runtime_error(ec.message());

        return fp;
    } else if (std::get_if<buffer_parser_t>(&parser)) {
        throw std::runtime_error("the parser is already converted to buffer parser");
    } else {
        throw std::runtime_error("unknown parser");
    }

    return *std::get<file_parser_t>(parser);
}

auto brb::Context::parser_buffer() -> request_parser<buffer_body> & {
    if (auto *p = std::get_if<buffer_parser_t>(&parser))
        return *p->get();
    parser = std::make_unique<request_parser<buffer_body>>(std::move(parser_empty()));
    return *std::get<buffer_parser_t>(parser);
}
