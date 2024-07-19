#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>

#include "build/httplib.h"

#include "build/app.h"
#include "build/index.h"
#include "build/style.h"

#include "gst.h"

using std::cout;
using std::endl;
using std::mutex;
using std::string;

using nlohmann::json;

using httplib::Request;
using httplib::Response;
using httplib::Server;

const string index_html{(const char *) build_index_bin};
const string style_css{(const char *) build_style_bin};
const string app_js{(const char *) build_app_bin};

const string ice = R"json(
[
    {
        "credentialType": "password",
        "urls": [
            "stun:stun.l.google.com:19302"
        ]
    },
    {
        "credential": "free",
        "credentialType": "password",
        "urls": [
            "turns:freeturn.tel:5349"
        ],
        "username": "free"
    }
]
)json";

namespace req_handlers {

void getIndexHtml(const Request & /*req*/, Response &res) {
    res.set_content(index_html, "text/html");
}

void getAppJs(const Request & /*req*/, Response &res) {
    res.set_content(app_js, "text/javascript");
}

void getStyleCss(const Request & /*req*/, Response &res) {
    res.set_content(style_css, "text/css");
}

void postIceServers(const Request & /*req*/, Response &res) {
    res.set_content(ice, "application/json");
}

static mutex sdp_mut;
static string sdp = "";

void doSignalling(const Request &req, Response &res) {
    assert(sdp != "");
    json ans = {
        {"type", "answer"},
        {"sdp",  sdp     }
    };
    res.set_content(ans.dump(), "application/json");
    // cout << "doSignalling: " << res.body << endl;
}

void set_offer_cb(const char *sdp_c, const char *type_c) {
    string type(type_c);
    assert(type == "answer");
    sdp = string(sdp_c);
    sdp_mut.unlock();
}

void postCreatePeerConnection(const Request &req, Response &res) {
    gst::create_pipeline();
    json offer = json::parse(req.body);

    sdp_mut.lock();
    gst::set_offer(offer["sdp"], set_offer_cb);
    sdp_mut.lock();
    gst::wait_ice_complete();
    doSignalling(req, res);
    sdp_mut.unlock();

    gst::start_pipeline();
}

void postStartVideo(const Request &req, Response &res) {
    // cout << "postStartVideo" << endl;
    sdp_mut.lock();
    gst::wait_ice_complete();
    doSignalling(req, res);
    sdp_mut.unlock();
}

void setup_server(Server &srv) {
    srv.Get("/", getIndexHtml);
    srv.Get("/index.html", getIndexHtml);
    srv.Get("/js/app.js", getAppJs);
    srv.Get("/css/style.css", getStyleCss);
    srv.Post("/iceServers", postIceServers);
    srv.Post("/createPeerConnection", postCreatePeerConnection);
    srv.Post("/startVideo", postStartVideo);
}

}; // namespace req_handlers

const int port = 8080;
const string address = "localhost";

int main(int argc, char *argv[]) {
    Server srv;

    if (!srv.is_valid()) {
        printf("server has an error...\n");
        return -1;
    }

    req_handlers::setup_server(srv);

    std::thread server_thread([&srv]() {
        cout << "Open http://" << address << ":" << port << " to access this demo" << endl;
        srv.listen(address, port);
    });

    cout << "Starting Gstreamer..." << endl;
    gst::init(&argc, &argv);
    gst::run();

    return 0;
}
