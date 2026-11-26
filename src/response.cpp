module;

#include <boost/beast.hpp>
#include <boost/asio.hpp>

module brb;

auto brb::Context::res() -> response_header & {
    return std::visit([](auto &r) -> response_header & { return r.base(); }, r);
}

auto brb::Context::response_empty() -> response<empty_body> & {
    if (auto *p = std::get_if<response<empty_body>>(&r)) {
        return *p;
    } else if (auto *p = std::get_if<response<string_body>>(&r)) {
        r = response<empty_body>(std::move(p->base()));
    } else if (auto *p = std::get_if<response<file_body>>(&r)) {
        r = response<empty_body>(std::move(p->base()));
    } else if (std::get_if<response<buffer_body>>(&r)) {
        throw std::runtime_error("cannot convert buffer body to empty body");
    }
    return std::get<response<empty_body>>(r);
}

auto brb::Context::response_string(std::optional<std::string> body) -> response<string_body> & {
    if (auto *p = std::get_if<response<empty_body>>(&r)) {
        auto res = response<string_body>(std::move(*p));
        if (body.has_value())
            res.body() = std::move(*body);

        r = std::move(res);
    } else if (auto *p = std::get_if<response<string_body>>(&r)) {
        return *p;
    } else if (auto *p = std::get_if<response<file_body>>(&r)) {
        auto res = response<string_body>(std::move(p->base()));

        if (body.has_value()) {
            res.body() = std::move(*body);
        } else if (p->body().file().is_open()) {
            res.body().resize(p->body().size());

            auto ec = beast::error_code();
            p->body().file().read(res.body().data(), res.body().size(), ec);

            if (ec)
                throw std::runtime_error(ec.message());
        }

        r = std::move(res);
    } else if (std::get_if<response<buffer_body>>(&r)) {
        throw std::runtime_error("cannot convert buffer body to string body");
    }

    return std::get<response<string_body>>(r);
}

auto brb::Context::response_file(const std::string &filepath) -> response<file_body> & {
    if (auto *p = std::get_if<response<empty_body>>(&r)) {
        auto res = response<file_body>(std::move(*p));

        if (!filepath.empty()) {
            auto ec = beast::error_code();
            res.body().open(filepath.c_str(), file_mode::scan, ec);

            if (ec)
                throw std::runtime_error(ec.message());
        }

        r = std::move(res);
    } else if (std::get_if<response<string_body>>(&r)) {
        throw std::runtime_error("cannot convert string body to file body without");
    } else if (auto *p = std::get_if<response<file_body>>(&r)) {
        if (filepath.empty())
            return *p;

        auto ec = beast::error_code();
        p->body().open(filepath.c_str(), file_mode::scan, ec);

        if (ec)
            throw std::runtime_error(ec.message());

        return *p;
    } else if (std::get_if<response<buffer_body>>(&r)) {
        throw std::runtime_error("cannot convert buffer body to file body");
    }

    return std::get<response<file_body>>(r);
}

auto brb::Context::response_buffer() -> std::pair<response<buffer_body> &, response_serializer<buffer_body> &> {
    if (auto *p = std::get_if<response<empty_body>>(&r)) {
        r = response<buffer_body>(std::move(*p));
    } else if (std::get_if<response<string_body>>(&r)) {
        throw std::runtime_error("cannot convert string body to buffer body");
    } else if (std::get_if<response<file_body>>(&r)) {
        throw std::runtime_error("cannot convert file body to buffer body");
    } else if (auto *p = std::get_if<response<buffer_body>>(&r)) {
        if (!serializer)
            serializer = std::make_unique<::brb::response_serializer<buffer_body>>(*p);

        return {*p, *serializer};
    }

    throw std::runtime_error("unknown response type");
}
