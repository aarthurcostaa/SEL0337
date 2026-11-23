#include "gst_io.hpp"

#include <chrono>
#include <cstring>

namespace {

int64_t realtime_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// Tira a primeira mensagem de erro pendente no bus (se houver).
bool pop_error(GstBus* bus, std::string& err) {
    GstMessage* msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
    if (!msg) return false;
    GError* gerr = nullptr;
    gchar* dbg = nullptr;
    gst_message_parse_error(msg, &gerr, &dbg);
    err = gerr ? gerr->message : "erro desconhecido";
    if (dbg) err += std::string(" (") + dbg + ")";
    if (gerr) g_error_free(gerr);
    g_free(dbg);
    gst_message_unref(msg);
    return true;
}

// Põe em PLAYING. Com wait=true espera a transição terminar (captura: até chegar o
// 1º quadro). Saídas NÃO podem esperar: um pipeline ao vivo só completa o PLAYING
// quando recebe o primeiro buffer, que só enviamos depois de abrir.
bool start(GstElement* pipeline, GstBus* bus, bool wait, std::string& err) {
    bool failed = gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE;
    if (!failed && wait)
        failed = gst_element_get_state(pipeline, nullptr, nullptr, 5 * GST_SECOND) == GST_STATE_CHANGE_FAILURE;
    if (failed) {
        if (!pop_error(bus, err)) err = "não foi possível iniciar o pipeline";
        return false;
    }
    return !pop_error(bus, err);
}

}  // namespace

// ===========================================================================
bool GstCapture::open(const std::string& description, std::string& err) {
    close();
    GError* gerr = nullptr;
    pipeline_ = gst_parse_launch(description.c_str(), &gerr);
    if (gerr) {  // pode vir com pipeline parcial; tratamos como erro
        err = gerr->message;
        g_error_free(gerr);
        close();
        return false;
    }
    sink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
    if (!sink_) { err = "pipeline sem 'appsink name=sink'"; close(); return false; }
    bus_ = gst_element_get_bus(pipeline_);
    if (!start(pipeline_, bus_, true, err)) { close(); return false; }
    return true;
}

GstCapture::Status GstCapture::read(Frame& f, int timeout_ms, std::string& err) {
    GstSample* sample = nullptr;
    g_signal_emit_by_name(sink_, "try-pull-sample", static_cast<GstClockTime>(timeout_ms) * GST_MSECOND, &sample);
    const int64_t now_rt = realtime_ns();
    if (!sample) {
        if (pop_error(bus_, err)) return Status::Error;
        gboolean eos = FALSE;
        g_object_get(sink_, "eos", &eos, nullptr);
        return eos ? Status::Eos : Status::Timeout;
    }

    GstStructure* s = gst_caps_get_structure(gst_sample_get_caps(sample), 0);
    int w = 0, h = 0, fn = 0, fd = 1;
    gst_structure_get_int(s, "width", &w);
    gst_structure_get_int(s, "height", &h);
    f.nominal_fps = (gst_structure_get_fraction(s, "framerate", &fn, &fd) && fd > 0) ? double(fn) / fd : 0.0;
    GstBuffer* buf = gst_sample_get_buffer(sample);
    const size_t size = gst_buffer_get_size(buf);
    const size_t row = static_cast<size_t>(w) * 3;

    // Buffer de um plano só: stride = tamanho / altura (cobre o padding de alinhamento).
    size_t stride = (row + 3) & ~static_cast<size_t>(3);
    if (h > 0 && size % h == 0 && size / h >= row) stride = size / h;
    if (w <= 0 || h <= 0 || stride * h > size) {
        gst_sample_unref(sample);
        err = "quadro com formato inesperado (esperava RGB)";
        return Status::Error;
    }

    f.width = w;
    f.height = h;
    f.rgb.resize(row * h);
    if (stride == row) {
        gst_buffer_extract(buf, 0, f.rgb.data(), row * h);
    } else {
        for (int y = 0; y < h; ++y) gst_buffer_extract(buf, y * stride, f.rgb.data() + y * row, row);
    }

    // Instante da captura. O libcamerasrc carimba o PTS a partir do timestamp do sensor
    // (em running time); a "idade" do quadro é running_time_agora - PTS.
    f.stamp_ns = now_rt;
    const GstClockTime pts = GST_BUFFER_PTS(buf);
    if (GST_CLOCK_TIME_IS_VALID(pts)) {
        if (GstClock* clock = gst_element_get_clock(pipeline_)) {
            const GstClockTime now = gst_clock_get_time(clock);
            const GstClockTime base = gst_element_get_base_time(pipeline_);
            gst_object_unref(clock);
            if (now >= base) {
                const int64_t age = static_cast<int64_t>(now - base) - static_cast<int64_t>(pts);
                if (age >= 0 && age < 2 * static_cast<int64_t>(GST_SECOND)) f.stamp_ns = now_rt - age;
            }
        }
    }
    gst_sample_unref(sample);
    return Status::Ok;
}

void GstCapture::close() {
    if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (sink_) gst_object_unref(sink_);
    if (bus_) gst_object_unref(bus_);
    if (pipeline_) gst_object_unref(pipeline_);
    pipeline_ = sink_ = nullptr;
    bus_ = nullptr;
}

// ===========================================================================
bool GstOutput::open(const std::string& tail, int width, int height, double fps, bool live, std::string& err) {
    close();
    width_ = width;
    height_ = height;
    stride_ = (width * 3 + 3) & ~3;  // stride padrão do GStreamer para RGB
    live_ = live;
    count_ = 0;

    const int fps_num = fps > 0.1 ? static_cast<int>(fps * 1000 + 0.5) : 1;
    const int fps_den = fps > 0.1 ? 1000 : 1;
    frame_dur_ = gst_util_uint64_scale(GST_SECOND, fps_den, fps_num);
    std::string desc = "appsrc name=src format=time block=false ";
    desc += live ? "is-live=true do-timestamp=true " : "is-live=false ";
    desc += "caps=\"video/x-raw,format=RGB,width=" + std::to_string(width) + ",height=" + std::to_string(height) +
            ",framerate=" + std::to_string(fps_num) + "/" + std::to_string(fps_den) + "\" ! ";
    if (live) desc += "queue leaky=downstream max-size-buffers=2 ! ";  // não acumula atraso
    desc += tail;

    GError* gerr = nullptr;
    pipeline_ = gst_parse_launch(desc.c_str(), &gerr);
    if (gerr) {
        err = gerr->message;
        g_error_free(gerr);
        close(0);
        return false;
    }
    src_ = gst_bin_get_by_name(GST_BIN(pipeline_), "src");
    bus_ = gst_element_get_bus(pipeline_);
    if (!start(pipeline_, bus_, false, err)) { close(0); return false; }
    return true;
}

bool GstOutput::push(const uint8_t* rgb, std::string& err) {
    if (!pipeline_) return false;
    const size_t row = static_cast<size_t>(width_) * 3;
    GstBuffer* buf = gst_buffer_new_allocate(nullptr, static_cast<size_t>(stride_) * height_, nullptr);
    if (static_cast<size_t>(stride_) == row) {
        gst_buffer_fill(buf, 0, rgb, row * height_);
    } else {
        for (int y = 0; y < height_; ++y) gst_buffer_fill(buf, y * stride_, rgb + y * row, row);
    }
    if (!live_) {  // ao vivo, o appsrc carimba sozinho (do-timestamp=true)
        GST_BUFFER_PTS(buf) = count_ * frame_dur_;
        GST_BUFFER_DURATION(buf) = frame_dur_;
    }
    ++count_;
    GstFlowReturn ret = GST_FLOW_OK;
    g_signal_emit_by_name(src_, "push-buffer", buf, &ret);  // o sinal não toma posse do buffer
    gst_buffer_unref(buf);
    if (pop_error(bus_, err)) return false;
    if (ret != GST_FLOW_OK) { err = "push-buffer falhou"; return false; }
    return true;
}

void GstOutput::close(int timeout_ms) {
    if (pipeline_ && src_ && timeout_ms > 0) {
        GstFlowReturn ret = GST_FLOW_OK;
        g_signal_emit_by_name(src_, "end-of-stream", &ret);
        GstMessage* msg = gst_bus_timed_pop_filtered(
            bus_, static_cast<GstClockTime>(timeout_ms) * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        if (msg) gst_message_unref(msg);
    }
    if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (src_) gst_object_unref(src_);
    if (bus_) gst_object_unref(bus_);
    if (pipeline_) gst_object_unref(pipeline_);
    pipeline_ = src_ = nullptr;
    bus_ = nullptr;
}
