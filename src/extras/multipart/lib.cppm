module;

#include <boost/asio.hpp>
#include <boost/beast.hpp>

export module brb.multipart;
import brb;

export namespace brb::multipart {
    struct Part {
        fields      headers;
        std::string body;

        std::string        dump(const std::string &boundary) const;
        static std::string dump_end(const std::string &boundary);
    };

    class Parser {
    public:
        explicit Parser(Context &c)
            : c(c)
            , boundary(c.multipart_boundary) {}

        // parsed part or error if not empty
        awaitable<std::pair<Part, std::string>> get();

        bool done() const;

    private:
        Context    &c;
        std::string buffer;
        int         state = 0;
        std::string boundary;
        Part        part;

        std::string feed(std::string_view input);
    };
} // namespace brb::multipart
