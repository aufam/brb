module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

module brb;

void brb::Context::vary(std::string_view value) {
    auto &res  = this->res();
    auto  vary = res[http::field::vary];
    if (vary.empty()) {
        res.set(http::field::vary, value);
    } else {
        res.set(http::field::vary, std::string(vary) + ", " + std::string(value));
    }
}
