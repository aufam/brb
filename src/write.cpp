module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

module brb;


auto brb::Context::write_chunk(std::string_view data, bool more) -> awaitable<void> {
    auto &res = response_buffer();
    if (!serializer) {
        serializer = std::make_unique<http::response_serializer<http::buffer_body>>(res);
    }

    res.body().data = const_cast<char *>(data.data());
    res.body().size = data.size();
    res.body().more = more;

    try {
        co_await http::async_write(*stream, *serializer);
    } catch (boost::system::system_error const &e) {
        if (e.code() != http::error::need_buffer)
            throw;
    }
}

auto brb::Context::write_response() -> awaitable<void> {
    if (aborted)
        co_return;

    if (auto *p = std::get_if<response<empty_body>>(&r)) {
        p->prepare_payload();
        co_await http::async_write(*stream, *p);
        aborted = true;
    } else if (auto *p = std::get_if<response<string_body>>(&r)) {
        p->prepare_payload();
        co_await http::async_write(*stream, *p);
        aborted = true;
    } else if (auto *p = std::get_if<response<file_body>>(&r)) {
        p->prepare_payload();
        co_await http::async_write(*stream, *p);
        aborted = true;
    } else if (std::get_if<response<buffer_body>>(&r)) {
        // do nothing
    }
}
