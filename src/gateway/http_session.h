#ifndef HTTP_SESSION_H
#define HTTP_SESSION_H

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/strand.hpp>
#include <memory> 
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;

public:
    explicit HttpSession(tcp::socket&& socket);
    void Run();

private:
    void DoRead();
    void OnRead(beast::error_code ec, std::size_t bytes_transferred);
    void HandleRequest();
    void DoWrite(http::response<http::string_body>&& res);
};

#endif
