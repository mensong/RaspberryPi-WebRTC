#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

std::string createConnectionParams(const std::string &accessToken) {
    return "token=" + accessToken;
}

int main() {
    try {
        net::io_context ioc;

        tcp::resolver resolver(ioc);
        websocket::stream<tcp::socket> ws(ioc);

        std::string host = "192.168.4.21";
        std::string port = "8080";
        std::string accessToken =
            "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
            "eyJleHAiOjE3NzM4OTI2NjEsImlzcyI6IkFQSVduUVRzNHRtVVp2QSIsIm5iZiI6MTc0MjM1NjY2MSwic3ViIj"
            "oiMTMxZGZjMzItNjRlMi00YjZiLTllZGEtZDdjYTU5NTNjYWJlIiwidmlkZW8iOnsicm9vbSI6ImRldmljZS0x"
            "Iiwicm9vbUpvaW4iOnRydWV9fQ.ThQTHYd8CBR0t3epwcak6oaleeu760V96UF8GbOMUks";
        std::string queryParams = createConnectionParams(accessToken);
        std::string target = "/rtc?" + queryParams;

        // WebSocket without SSL
        auto const results = resolver.resolve(host, port);
        net::connect(ws.next_layer(), results.begin(), results.end());
        ws.handshake(host, target);
        std::cout << "WebSocket is connected!" << std::endl;

        while (true) {
            beast::flat_buffer buffer;
            try {
                ws.read(buffer);
            } catch (const std::exception &e) {
                std::cerr << "Error: " << e.what() << std::endl;
                break;
            }

            if (ws.got_text()) {
                std::string res = beast::buffers_to_string(buffer.data());

                json jsonObj = json::parse(res.c_str());
                std::string action = jsonObj["Action"];
                std::string message = jsonObj["Message"];
                std::cout << "Received Action: " << action << std::endl;
                std::cout << "Received Message: " << message << std::endl;

                if (action == "join") {
                    std::string response = "{\"Action\":\"offer\",\"Message\":\"test\"}";
                    ws.write(net::buffer(response));
                } else {
                    break;
                }
            } else if (ws.got_binary()) {
                std::vector<uint8_t> binaryData(boost::asio::buffers_begin(buffer.data()),
                                                boost::asio::buffers_end(buffer.data()));
                std::cout << "Received Binary Data (size: " << binaryData.size() << " bytes)"
                          << std::endl;
            }
        }

        ws.close(websocket::close_code::normal);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}
