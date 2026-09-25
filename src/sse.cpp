module;

#include <boost/beast.hpp>
#include <boost/asio.hpp>

module brb;

static std::optional<brb::SSE> feed_sse(std::string_view input, std::string &buffer, bool &sse_has_data, brb::SSE &sse) {
    buffer.append(input);

    for (;;) {
        const auto pos = buffer.find('\n');

        if (pos == std::string::npos)
            return std::nullopt;

        std::string line = buffer.substr(0, pos);
        buffer.erase(0, pos + 1);

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty()) {
            if (!sse_has_data)
                continue;

            if (!sse.data.empty() && sse.data.back() == '\n')
                sse.data.pop_back();

            sse_has_data = false;

            return std::exchange(sse, {});
        }

        if (line.front() == ':')
            continue;

        const auto colon = line.find(':');

        std::string_view field = line;
        std::string_view value;

        if (colon != std::string_view::npos) {
            field = std::string_view(line).substr(0, colon);
            value = std::string_view(line).substr(colon + 1);

            if (!value.empty() && value.front() == ' ')
                value.remove_prefix(1);
        }

        if (field == "event") {
            sse.event.assign(value);
        } else if (field == "data") {
            sse.data.append(value);
            sse.data.push_back('\n');
            sse_has_data = true;
        } else if (field == "id") {
            if (value.find('\0') == std::string_view::npos)
                sse.id.assign(value);
        } else if (field == "retry") {
            // Parse only if entirely decimal.
            std::uint64_t retry = 0;

            if (!value.empty()) {
                bool valid = true;

                for (char c : value) {
                    if (c < '0' || c > '9') {
                        valid = false;
                        break;
                    }

                    retry = retry * 10 + static_cast<unsigned>(c - '0');
                }

                if (valid)
                    sse.retry = std::chrono::milliseconds(retry);
            }
        }
    }
}

auto brb::Context::parse_sse() -> awaitable<SSE> {
    auto &parser = parser_buffer();

    char         buffer[8192];
    flat_buffer &flat_buffer = this->buffer;

    for (;;) {
        parser.get().body().data = buffer;
        parser.get().body().size = sizeof(buffer);

        co_await http::async_read_some(*stream, flat_buffer, parser);

        std::string_view input(buffer, sizeof(buffer) - parser.get().body().size);
        if (auto result = feed_sse(input, stream_buffer, sse_has_data, sse))
            co_return std::move(*result);

        if (parser.is_done())
            throw std::runtime_error("SSE stream ended before a complete event");
    }
}

std::string brb::SSE::dump() const {
    if (event.empty() && data.empty())
        return {};

    std::string out;

    if (!id.empty())
        out += "id: " + id + "\n";

    if (!event.empty())
        out += "event: " + event + "\n";

    if (!data.empty())
        out += "data: " + data + "\n";

    if (retry.has_value())
        out += "retry: " + std::to_string(retry->count()) + "\n";

    out += "\n";

    return out;
}
