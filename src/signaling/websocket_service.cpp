#include "signaling/websocket_service.h"

#include <nlohmann/json.hpp>

#include "common/logging.h"

const int MAX_RETRIES = 10;

std::shared_ptr<WebsocketService> WebsocketService::Create(Args args,
                                                           std::shared_ptr<Conductor> conductor,
                                                           boost::asio::io_context &ioc) {
    return std::make_shared<WebsocketService>(args, conductor, ioc);
}

WebsocketService::WebsocketService(Args args, std::shared_ptr<Conductor> conductor,
                                   boost::asio::io_context &ioc)
    : SignalingService(conductor),
      args_(args),
      resolver_(ioc),
      ws_(ioc) {}

WebsocketService::~WebsocketService() { Disconnect(); }

void WebsocketService::OnRemoteIce(const std::string &message) {
    nlohmann::json res = nlohmann::json::parse(message);
    std::string target = res["target"];
    std::string canditateInit = res["candidateInit"];

    nlohmann::json canditateObj = nlohmann::json::parse(canditateInit);
    std::string sdp_mid = canditateObj["sdpMid"];
    int sdp_mline_index = canditateObj["sdpMLineIndex"];
    std::string candidate = canditateObj["candidate"];
    DEBUG_PRINT("Received remote ICE: %s, %d, %s", sdp_mid.c_str(), sdp_mline_index,
                candidate.c_str());

    if (target == "PUBLISHER") {
        pub_peer_->SetRemoteIce(sdp_mid, sdp_mline_index, candidate);
    } else if (target == "SUBSCRIBER") {
        sub_peer_->SetRemoteIce(sdp_mid, sdp_mline_index, candidate);
    }
}

void WebsocketService::Connect() {
    resolver_.async_resolve(
        args_.ws_host, std::to_string(args_.ws_port),
        [this](boost::system::error_code ec, tcp::resolver::results_type results) {
            OnResolve(ec, results);
        });
}

void WebsocketService::Disconnect() {
    if (ws_.is_open()) {
        ws_.async_close(websocket::close_code::normal, [this](boost::system::error_code ec) {
            if (ec) {
                ERROR_PRINT("Close Error: %s", ec.message().c_str());
            } else {
                INFO_PRINT("WebSocket Closed");
            }
        });
    } else {
        INFO_PRINT("WebSocket already closed");
    }
}

void WebsocketService::OnResolve(boost::system::error_code ec,
                                 tcp::resolver::results_type results) {
    if (!ec) {
        net::async_connect(ws_.next_layer(), results,
                           [this](boost::system::error_code ec, tcp::endpoint) {
                               OnConnect(ec);
                           });
    } else {
        ERROR_PRINT("Failed to resolve: %s", ec.message().c_str());
        return;
    }
}

void WebsocketService::OnConnect(boost::system::error_code ec) {
    if (!ec) {
        std::string target = "/rtc?token=" + args_.ws_token;
        ws_.async_handshake(args_.ws_host, target, [this](boost::system::error_code ec) {
            OnHandshake(ec);
        });
    } else {
        ERROR_PRINT("Connection Error: %s", ec.message().c_str());
    }
}

void WebsocketService::OnHandshake(boost::system::error_code ec) {
    if (!ec) {
        INFO_PRINT("WebSocket is connected!");
        Read();
    } else {
        ERROR_PRINT("Handshake Error: %s", ec.message().c_str());
    }
}

void WebsocketService::Read() {
    ws_.async_read(buffer_, [this](boost::system::error_code ec, std::size_t bytes_transferred) {
        if (!ec) {
            std::string req = beast::buffers_to_string(buffer_.data());
            OnMessage(req);
            buffer_.consume(bytes_transferred);
            Read();
        } else {
            ERROR_PRINT("Read Error: %s", ec.message().c_str());
            Disconnect();
        }
    });
}

void WebsocketService::OnMessage(const std::string &req) {
    json jsonObj = json::parse(req.c_str());
    std::string action = jsonObj["action"];
    std::string message = jsonObj["message"];
    DEBUG_PRINT("Received message: %s", req.c_str());

    if (action == "join") {
        PeerConfig config;
        webrtc::PeerConnectionInterface::IceServer ice_server;

        nlohmann::json messageJson = nlohmann::json::parse(jsonObj["message"].get<std::string>());
        ice_server.urls = messageJson["urls"].get<std::vector<std::string>>();
        ice_server.username = messageJson["username"];
        ice_server.password = messageJson["credential"];
        config.servers.push_back(ice_server);

        pub_peer_ = conductor->CreatePeerConnection(config);
        pub_peer_->OnLocalSdp(
            [this](const std::string &peer_id, const std::string &sdp, const std::string &type) {
                Write(type, sdp);
            });
        pub_peer_->OnLocalIce([this](const std::string &peer_id, const std::string &sdp_mid,
                                     int sdp_mline_index, const std::string &candidate) {
            Write("trickle", candidate);
        });

        config.is_publisher = false;
        sub_peer_ = conductor->CreatePeerConnection(config);
        sub_peer_->OnLocalSdp(
            [this](const std::string &peer_id, const std::string &sdp, const std::string &type) {
                Write(type, sdp);
            });
        sub_peer_->OnLocalIce([this](const std::string &peer_id, const std::string &sdp_mid,
                                     int sdp_mline_index, const std::string &candidate) {
            Write("trickle", candidate);
        });

        Write("addVideoTrack", args_.uid);
        if (!args_.no_audio) {
            Write("addAudioTrack", args_.uid);
        }
    } else if (action == "offer" && !sub_peer_->IsConnected()) {
        sub_peer_->SetRemoteSdp(message, "offer");
    } else if (action == "answer" && !pub_peer_->IsConnected()) {
        pub_peer_->SetRemoteSdp(message, "answer");
    } else if (action == "trackPublished") {
        pub_peer_->CreateOffer();
    } else if (action == "trickle") {
        OnRemoteIce(message);
    } else if (action == "leave") {
        Disconnect();
    }
}

void WebsocketService::Write(const std::string &action, const std::string &message) {
    nlohmann::json request_json;
    request_json["action"] = action;
    request_json["message"] = message;
    std::string request = request_json.dump();

    std::lock_guard<std::mutex> lock(write_mutex_);
    bool writing_in_progress = !write_queue_.empty();
    write_queue_.push_back(request);

    if (!writing_in_progress) {
        DoWrite();
    }
}

void WebsocketService::DoWrite() {
    if (write_queue_.empty())
        return;

    ws_.async_write(boost::asio::buffer(write_queue_.front()),
                    [this](boost::system::error_code ec, std::size_t bytes_transferred) {
                        std::lock_guard<std::mutex> lock(write_mutex_);
                        if (!ec) {
                            write_queue_.pop_front();
                            if (!write_queue_.empty()) {
                                DoWrite();
                            }
                        } else {
                            ERROR_PRINT("Write Error: %s", ec.message().c_str());
                            Disconnect();
                        }
                    });
}
