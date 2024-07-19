#include <cassert>
#include <mutex>
#include <string>

#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include <gst/sdp/sdp.h>

#include <gst/webrtc/nice/nice.h>
#include <gst/webrtc/webrtc.h>

#include "gst.h"

namespace gst {
static GMainLoop *loop;
static GstElement *pipe, *webrtc, *audio_bin, *video_bin = NULL;
static GObject *messages_channel;

#define STUN_SERVER "stun://stun.l.google.com:19302"
// #define TURN_SERVER "turn://free:free@freeturn.tel:5349"

static std::string audio_desc = "audiotestsrc is-live=true wave=red-noise ! audioconvert ! audioresample ! queue ! opusenc perfect-timestamp=true ! rtpopuspay name=audiopay pt=96 ! application/x-rtp, encoding-name=OPUS ! queue";
static std::string video_desc = "ximagesrc display-name=:0.0 remote=1 blocksize=16384 use-damage=0 show-pointer=true ! videoconvert ! video/x-raw,format=I420 ! queue ! x264enc name=vcodec threads=8 aud=false b-adapt=false key-int-max=60 sliced-threads=true byte-stream=true tune=zerolatency speed-preset=veryfast bitrate=20000 pass=17 ! rtph264pay aggregate-mode=zero-latency config-interval=-1 ! queue";

void init(int *argc, char **argv[]) {
    // TODO: Do we need to inject any options here instead?
    gst_init(argc, argv);
}

void run() {
    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    if (loop) {
        g_main_loop_unref(loop);
    }

    if (pipe) {
        GstBus *bus;

        gst_element_set_state(GST_ELEMENT(pipe), GST_STATE_NULL);
        gst_print("Pipeline stopped\n");

        bus = gst_pipeline_get_bus(GST_PIPELINE(pipe));
        gst_bus_remove_watch(bus);
        gst_object_unref(bus);

        gst_object_unref(pipe);
    }
}

static void on_negotiation_needed(GstElement *element, gpointer user_data) {
    // gboolean create_offer = GPOINTER_TO_INT (user_data);
    //
    // if (remote_is_offerer) {
    //   soup_websocket_connection_send_text (ws_conn, "OFFER_REQUEST");
    // } else if (create_offer) {
    //   GstPromise *promise =
    //       gst_promise_new_with_change_func (on_offer_created, NULL, NULL);
    //   g_signal_emit_by_name(webrtc, "create-offer", NULL, promise);
    // }
    //  TODO: Can we ask client to /startVideo?
}

static gboolean cleanup_and_quit_loop(const gchar *msg) {
    if (msg)
        gst_printerr("%s\n", msg);

    if (loop) {
        g_main_loop_quit(loop);
        g_clear_pointer(&loop, g_main_loop_unref);
    }

    /* To allow usage as a GSourceFunc */
    return G_SOURCE_REMOVE;
}

static gboolean bus_watch_cb(GstBus *bus, GstMessage *message, gpointer user_data) {
    GstPipeline *pipeline = (GstPipeline *) user_data;

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR:
            {
                GError *error = NULL;
                gchar *debug = NULL;

                gst_message_parse_error(message, &error, &debug);
                cleanup_and_quit_loop("ERROR: Error on bus");
                g_warning("Error on bus: %s (debug: %s)", error->message, debug);
                g_error_free(error);
                g_free(debug);
                break;
            }
        case GST_MESSAGE_WARNING:
            {
                GError *error = NULL;
                gchar *debug = NULL;

                gst_message_parse_warning(message, &error, &debug);
                g_warning("Warning on bus: %s (debug: %s)", error->message, debug);
                g_error_free(error);
                g_free(debug);
                break;
            }
        case GST_MESSAGE_LATENCY:
            gst_bin_recalculate_latency(GST_BIN(pipeline));
            break;
        default:
            break;
    }

    return G_SOURCE_CONTINUE;
}

static void data_channel_on_error(GObject *dc, gpointer user_data) {
    // TODO: Report an error without failing?
    cleanup_and_quit_loop("Data channel error");
}

static void data_channel_on_open(GObject *dc, gpointer user_data) {
    GBytes *bytes = g_bytes_new("data", strlen("data"));
    // gst_print("data channel opened\n");
    // g_signal_emit_by_name(dc, "send-string", "Hi! from GStreamer");
    // g_signal_emit_by_name(dc, "send-data", bytes);
    // g_bytes_unref(bytes);
}

static void data_channel_on_close(GObject *dc, gpointer user_data) {
    // TODO: do we even need to have messages_channel?
    messages_channel = NULL;
}

static void data_channel_on_message_string(GObject *dc, gchar *str, gpointer user_data) {
    // TODO: Handle messages
    gst_print("Received data channel message: %s\n", str);
}

static void connect_data_channel_signals(GObject *data_channel) {
    g_signal_connect(data_channel, "on-error", G_CALLBACK(data_channel_on_error), NULL);
    g_signal_connect(data_channel, "on-open", G_CALLBACK(data_channel_on_open), NULL);
    g_signal_connect(data_channel, "on-close", G_CALLBACK(data_channel_on_close), NULL);
    g_signal_connect(data_channel, "on-message-string", G_CALLBACK(data_channel_on_message_string), NULL);
}

static void on_data_channel(GstElement *webrtc, GObject *data_channel, gpointer user_data) {
    connect_data_channel_signals(data_channel);
    messages_channel = data_channel;
}

std::mutex gathering_mut;

static void on_ice_gathering_state_notify(GstElement *webrtcbin, GParamSpec *pspec, gpointer user_data) {
    GstWebRTCICEGatheringState ice_gather_state;
    const gchar *new_state = "unknown";

    g_object_get(webrtcbin, "ice-gathering-state", &ice_gather_state, NULL);
    switch (ice_gather_state) {
        case GST_WEBRTC_ICE_GATHERING_STATE_NEW:
            new_state = "new";
            break;
        case GST_WEBRTC_ICE_GATHERING_STATE_GATHERING:
            new_state = "gathering";
            break;
        case GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE:
            gathering_mut.unlock();
            new_state = "complete";
            break;
    }

    gst_print("ICE gathering state changed to %s\n", new_state);
}

void create_pipeline() {
    GstBus *bus;
    GError *audio_error = NULL;
    GError *video_error = NULL;
    pipe = gst_pipeline_new("webrtc-pipeline");
    audio_bin = gst_parse_bin_from_description(audio_desc.c_str(), TRUE, &audio_error);
    video_bin = gst_parse_bin_from_description(video_desc.c_str(), TRUE, &video_error);

    if (audio_error) {
        gst_printerr("Failed to parse audio_bin: %s\n", audio_error->message);
        g_error_free(audio_error);
        abort(); // TODO
    }

    if (video_error) {
        gst_printerr("Failed to parse video_bin: %s\n", video_error->message);
        g_error_free(video_error);
        abort(); // TODO
    }

    webrtc = gst_element_factory_make_full("webrtcbin", "name", "sendrecv", "stun-server", STUN_SERVER, /*"turn-server", TURN_SERVER,*/ "latency", "0", NULL);

    gst_bin_add_many(GST_BIN(pipe), audio_bin, video_bin, webrtc, NULL);

    if (!gst_element_link(audio_bin, webrtc)) {
        gst_printerr("Failed to link audio_bin\n");
        abort(); // TODO
    }
    if (!gst_element_link(video_bin, webrtc)) {
        gst_printerr("Failed to link video_bin\n");
        abort(); // TODO
    }

    /* This is the gstwebrtc entry point where we create the offer and so on. It
     * will be called when the pipeline goes to PLAYING. */
    g_signal_connect(webrtc, "on-negotiation-needed", G_CALLBACK(on_negotiation_needed), NULL);
    // TODO: Doesn't seem to be used by app.js
    // g_signal_connect(webrtc, "on-ice-candidate", G_CALLBACK(send_ice_candidate_message), NULL);
    g_signal_connect(webrtc, "notify::ice-gathering-state", G_CALLBACK(on_ice_gathering_state_notify), NULL);

    bus = gst_pipeline_get_bus(GST_PIPELINE(pipe));
    gst_bus_add_watch(bus, bus_watch_cb, pipe);
    gst_object_unref(bus);

    gst_element_set_state(pipe, GST_STATE_READY);

    // g_signal_emit_by_name(webrtc, "create-data-channel", "channel", NULL, &send_channel);
    // if (send_channel) {
    //     gst_print("Created data channel\n");
    //     connect_data_channel_signals(send_channel);
    // } else {
    //     gst_print("Could not create data channel, is usrsctp available?\n");
    // }

    g_signal_connect(webrtc, "on-data-channel", G_CALLBACK(on_data_channel), NULL);
    // g_signal_connect(webrtc, "pad-added", G_CALLBACK(on_incoming_stream), pipe);
}

void start_pipeline() {
    int ret = gst_element_set_state(GST_ELEMENT(pipe), GST_STATE_PLAYING);
    assert(ret != GST_STATE_CHANGE_FAILURE);
}

static void send_sdp_to_peer(const offer_cb_t cb, GstWebRTCSessionDescription *desc) {
    gchar *text = gst_sdp_message_as_text(desc->sdp);
    const char *type = NULL;
    if (desc->type == GST_WEBRTC_SDP_TYPE_OFFER) {
        gst_print("Sending offer\n"); //:\n%s\n", text);
        type = "offer";
    } else if (desc->type == GST_WEBRTC_SDP_TYPE_ANSWER) {
        gst_print("Sending answer\n"); //:\n%s\n", text);
        type = "answer";
    } else {
        g_assert_not_reached();
    }

    cb(text, type);
    g_free(text);
}

static void on_answer_created(GstPromise *promise, gpointer user_data) {
    GstWebRTCSessionDescription *answer = NULL;
    const GstStructure *reply;

    g_assert_cmphex(gst_promise_wait(promise), ==, GST_PROMISE_RESULT_REPLIED);
    reply = gst_promise_get_reply(promise);
    gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
    gst_promise_unref(promise);

    promise = gst_promise_new();
    g_signal_emit_by_name(webrtc, "set-local-description", answer, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);

    /* Send answer to peer */
    send_sdp_to_peer((const offer_cb_t) user_data, answer);
    gst_webrtc_session_description_free(answer);
}

static void on_offer_set(GstPromise *promise, gpointer user_data) {
    gst_promise_unref(promise);
    promise = gst_promise_new_with_change_func(on_answer_created, user_data, NULL);
    g_signal_emit_by_name(webrtc, "create-answer", NULL, promise);
}

void set_offer(std::string offer, const offer_cb_t cb) {
    gathering_mut.lock();
    GstSDPMessage *sdp = NULL;
    int ret = gst_sdp_message_new(&sdp);
    g_assert_cmphex(ret, ==, GST_SDP_OK);
    ret = gst_sdp_message_parse_buffer((guint8 *) offer.c_str(), offer.length(), sdp);
    g_assert_cmphex(ret, ==, GST_SDP_OK);

    GstWebRTCSessionDescription *desc = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);

    GstPromise *promise = gst_promise_new_with_change_func(on_offer_set, (gpointer) cb, NULL);
    g_signal_emit_by_name(webrtc, "set-remote-description", desc, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);
    gst_webrtc_session_description_free(desc);
    // gst_sdp_message_free(sdp);
}

void wait_ice_complete() {
    gathering_mut.lock();
    gathering_mut.unlock();
}

} // namespace gst
