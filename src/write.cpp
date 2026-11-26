module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

module brb;


auto brb::Context::write_chunk(std::string_view data, bool more) -> awaitable<void> {
    auto [res, ser] = response_buffer();

    res.body().data = const_cast<char *>(data.data());
    res.body().size = data.size();
    res.body().more = more;

    beast::error_code ec;
    co_await async_write(*stream, ser);
    if (ec && ec != error::need_buffer)
        throw boost::system::system_error(ec);
}

auto brb::Context::write_response() -> awaitable<void> {
    if (aborted)
        co_return;

    if (auto *p = std::get_if<response<empty_body>>(&r)) {
        p->prepare_payload();
        co_await async_write(*stream, *p);
        aborted = true;
    } else if (auto *p = std::get_if<response<string_body>>(&r)) {
        p->prepare_payload();
        co_await async_write(*stream, *p);
        aborted = true;
    } else if (auto *p = std::get_if<response<file_body>>(&r)) {
        p->prepare_payload();
        co_await async_write(*stream, *p);
        aborted = true;
    } else if (std::get_if<response<buffer_body>>(&r)) {
        // do nothing
    }
}
