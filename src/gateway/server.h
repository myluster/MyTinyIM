#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include "http_session.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class Server : public std::enable_shared_from_this<Server> {
    net::io_context& ioc_;
    tcp::acceptor acceptor_;

public:
    Server(net::io_context& ioc, tcp::endpoint endpoint)
        : ioc_(ioc), acceptor_(ioc) {
        
        beast::error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        if(ec) { spdlog::error("Open failed: {}", ec.message()); return; }
        
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        acceptor_.bind(endpoint, ec);
        if(ec) { spdlog::error("Bind failed: {}", ec.message()); return; }
        
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if(ec) { spdlog::error("Listen failed: {}", ec.message()); return; }
    }

    void Run() {
        DoAccept();
    }

private:
    void DoAccept() {
        acceptor_.async_accept(net::make_strand(ioc_),
            beast::bind_front_handler(&Server::OnAccept, shared_from_this()));
    }

    void OnAccept(beast::error_code ec, tcp::socket socket) {
        if(ec) {
            spdlog::error("Accept failed: {}", ec.message());
        } else {
            std::make_shared<HttpSession>(std::move(socket))->Run();
        }
        
        // Accept next
        DoAccept();
    }
};
