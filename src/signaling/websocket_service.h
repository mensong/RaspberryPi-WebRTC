#ifndef WEBSOCKET_SERVICE_H_
#define WEBSOCKET_SERVICE_H_

#include "signaling/signaling_service.h"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include "args.h"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class WebsocketService : public SignalingService {
  public:
    static std::shared_ptr<WebsocketService> Create(Args args, std::shared_ptr<Conductor> conductor,
                                                    boost::asio::io_context &ioc);

    WebsocketService(Args args, std::shared_ptr<Conductor> conductor, boost::asio::io_context &ioc);
    ~WebsocketService();

  protected:
    void Connect() override;
    void Disconnect() override;

  private:
    Args args_;
    tcp::resolver resolver_;
    beast::flat_buffer buffer_;
    websocket::stream<tcp::socket> ws_;
    std::deque<std::string> write_queue_;
    std::mutex write_mutex_;
    rtc::scoped_refptr<RtcPeer> pub_peer_;
    rtc::scoped_refptr<RtcPeer> sub_peer_;

    void OnRemoteIce(const std::string &message);
    void OnResolve(boost::system::error_code ec, tcp::resolver::results_type results);
    void OnConnect(boost::system::error_code ec);
    void OnHandshake(boost::system::error_code ec);
    void OnMessage(const std::string &req);
    void Read();
    void Write(const std::string &action, const std::string &message);
    void DoWrite();
};

#endif
