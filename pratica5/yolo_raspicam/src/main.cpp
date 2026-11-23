// Detector YOLO11n (ncnn) na Raspberry Pi 3 com a raspicam, em C++ — sem OpenCV.
//
// Equivalente ao nó Python vision_detector_node, sem ROS:
//   - captura pela libcamera via GStreamer (libcamerasrc -> appsink);
//   - detecções saem como uma linha JSON por quadro no stdout (e/ou via UDP);
//   - lista vazia também é emitida, para distinguir "nada no quadro" de "detector parado";
//   - o frame anotado só é desenhado se houver alguma saída de vídeo (janela, arquivo, stream).
//
// Exemplos:
//   ./yolo_raspicam --model ../model_ncnn_model
//   ./yolo_raspicam --model ../model_ncnn_model --udp 192.168.4.2:5005 --quiet
//   ./yolo_raspicam --model ../model_ncnn_model --stream 192.168.4.2:5000
//   ./yolo_raspicam --model ../model_ncnn_model --save saida.avi
//   ./yolo_raspicam --model ../model_ncnn_model --image foto.jpg

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gst/gst.h>

#include "draw.hpp"
#include "gst_io.hpp"
#include "yolo_ncnn.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

// ---------------------------------------------------------------------------
// Argumentos
// ---------------------------------------------------------------------------
struct Args {
    std::string model = "model_ncnn_model";
    std::string source = "libcamera";  // libcamera | índice V4L2 | arquivo | pipeline GStreamer
    std::string image;                 // modo foto única
    int width = 640, height = 480, fps = 30;
    float conf = 0.35f, iou = 0.7f;
    int threads = 4;
    bool show = false;
    bool draw_fps = true;
    bool quiet = false;
    std::string save;                  // .avi / .mkv (ou .jpg/.png no modo --image)
    std::string stream;                // host:porta (RTP/JPEG por UDP)
    std::string sink;                  // trecho de pipeline GStreamer personalizado
    std::string udp;                   // host:porta para o JSON
    long max_frames = 0;
};

void usage(const char* prog) {
    std::fprintf(stderr,
        "Uso: %s [opções]\n"
        "  --model DIR        pasta do export ncnn (padrão: model_ncnn_model)\n"
        "  --source S         libcamera (padrão) | 0 (V4L2/USB) | video.mp4 | pipeline GStreamer\n"
        "  --width W --height H --fps F   resolução da câmera (padrão 640x480@30)\n"
        "  --image FOTO       roda numa imagem só e salva o resultado (--save, padrão resultado.jpg)\n"
        "  --conf C           limiar de confiança (padrão 0.35)\n"
        "  --iou I            limiar do NMS (padrão 0.7)\n"
        "  --threads N        threads do ncnn (padrão 4)\n"
        "  --show             janela com o vídeo anotado (precisa de display)\n"
        "  --save ARQ         grava o vídeo anotado (.avi ou .mkv, MJPEG)\n"
        "  --stream IP:PORTA  envia o vídeo anotado por RTP/JPEG via UDP\n"
        "  --sink \"PIPE\"      saída GStreamer personalizada (recebe RGB), ex.: \"videoconvert ! x264enc ! ...\"\n"
        "  --no-fps           não desenha o FPS no frame\n"
        "  --udp IP:PORTA     envia o JSON de cada quadro por UDP\n"
        "  --quiet            não imprime o JSON no stdout\n"
        "  --max-frames N     para depois de N quadros (benchmark)\n"
        "Opções do GStreamer (ex.: --gst-debug=2) também são aceitas.\n",
        prog);
}

bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "faltou valor para %s\n", name); std::exit(2); }
            return argv[++i];
        };
        if (k == "--model") a.model = next("--model");
        else if (k == "--source") a.source = next("--source");
        else if (k == "--image") a.image = next("--image");
        else if (k == "--width") a.width = std::atoi(next("--width"));
        else if (k == "--height") a.height = std::atoi(next("--height"));
        else if (k == "--fps") a.fps = std::atoi(next("--fps"));
        else if (k == "--conf") a.conf = std::strtof(next("--conf"), nullptr);
        else if (k == "--iou") a.iou = std::strtof(next("--iou"), nullptr);
        else if (k == "--threads") a.threads = std::atoi(next("--threads"));
        else if (k == "--show") a.show = true;
        else if (k == "--save") a.save = next("--save");
        else if (k == "--stream") a.stream = next("--stream");
        else if (k == "--sink") a.sink = next("--sink");
        else if (k == "--no-fps") a.draw_fps = false;
        else if (k == "--udp") a.udp = next("--udp");
        else if (k == "--quiet") a.quiet = true;
        else if (k == "--max-frames") a.max_frames = std::atol(next("--max-frames"));
        else if (k == "-h" || k == "--help") { usage(argv[0]); std::exit(0); }
        else { std::fprintf(stderr, "opção desconhecida: %s\n", k.c_str()); usage(argv[0]); return false; }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------
bool ends_with(const std::string& s, const char* suf) {
    const size_t n = std::strlen(suf);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suf[i]) return false;
    return true;
}

std::string quote(const std::string& s) {  // valor entre aspas para o gst_parse_launch
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
    return o + "\"";
}

bool split_host_port(const std::string& hp, std::string& host, int& port) {
    const auto colon = hp.rfind(':');
    if (colon == std::string::npos) return false;
    host = hp.substr(0, colon);
    port = std::atoi(hp.c_str() + colon + 1);
    return !host.empty() && port > 0 && port < 65536;
}

// Final comum: converte para RGB e entrega ao appsink.
// Ao vivo: max-buffers=1 drop=true => sempre o quadro MAIS RECENTE, sem acumular atraso
// quando a inferência é mais lenta que a câmera. Arquivo: processa todos os quadros.
std::string appsink_tail(bool live) {
    return std::string(" ! videoconvert ! video/x-raw,format=RGB ! appsink name=sink sync=false ") +
           (live ? "max-buffers=1 drop=true" : "max-buffers=2 drop=false");
}

std::string source_pipeline(const Args& a, bool& live) {
    const std::string res = "width=" + std::to_string(a.width) + ",height=" + std::to_string(a.height);
    if (!a.image.empty()) {
        live = false;
        return "filesrc location=" + quote(a.image) + " ! decodebin" + appsink_tail(false);
    }
    if (a.source == "libcamera") {  // raspicam pela pilha libcamera
        live = true;
        return "libcamerasrc ! video/x-raw," + res + ",framerate=" + std::to_string(a.fps) + "/1" +
               appsink_tail(true);
    }
    if (!a.source.empty() && a.source.find_first_not_of("0123456789") == std::string::npos) {
        live = true;  // webcam USB (ou pilha legada da câmera em /dev/video0)
        return "v4l2src device=/dev/video" + a.source + " ! decodebin ! videoconvert ! videoscale ! video/x-raw," +
               res + appsink_tail(true);
    }
    if (a.source.find('!') != std::string::npos) {  // trecho de pipeline do usuário
        live = a.source.find("filesrc") == std::string::npos;
        return a.source + appsink_tail(live);
    }
    live = false;  // arquivo de vídeo
    return "filesrc location=" + quote(a.source) + " ! decodebin" + appsink_tail(false);
}

struct OutputSlot {
    std::string name, tail;
    GstOutput out;
    bool failed = false;
};

// Monta a lista de saídas de vídeo pedidas na linha de comando.
bool build_outputs(const Args& a, bool image_mode, std::vector<std::unique_ptr<OutputSlot>>& outs) {
    auto add = [&](const std::string& name, const std::string& tail) {
        auto s = std::make_unique<OutputSlot>();
        s->name = name;
        s->tail = tail;
        outs.push_back(std::move(s));
    };
    if (image_mode) {
        const std::string path = a.save.empty() ? "resultado.jpg" : a.save;
        if (ends_with(path, ".png")) add(path, "videoconvert ! pngenc ! filesink location=" + quote(path));
        else add(path, "videoconvert ! jpegenc ! filesink location=" + quote(path));
        return true;
    }
    if (a.show) {
        if (std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY"))
            add("janela", "videoconvert ! autovideosink sync=false");
        else
            std::fprintf(stderr, "[aviso] sem display (DISPLAY/WAYLAND_DISPLAY): janela desabilitada. "
                                 "Use --stream ou --save.\n");
    }
    if (!a.save.empty()) {
        if (ends_with(a.save, ".avi"))
            add(a.save, "videoconvert ! video/x-raw,format=I420 ! jpegenc ! avimux ! filesink location=" + quote(a.save));
        else if (ends_with(a.save, ".mkv"))
            add(a.save, "videoconvert ! video/x-raw,format=I420 ! jpegenc ! matroskamux ! filesink location=" + quote(a.save));
        else { std::fprintf(stderr, "--save: use .avi ou .mkv\n"); return false; }
    }
    if (!a.stream.empty()) {
        std::string host;
        int port;
        if (!split_host_port(a.stream, host, port)) { std::fprintf(stderr, "--stream: use IP:PORTA\n"); return false; }
        add("stream " + a.stream, "videoconvert ! video/x-raw,format=I420 ! jpegenc quality=70 ! rtpjpegpay ! udpsink host=" + host +
                                      " port=" + std::to_string(port) + " sync=false");
    }
    if (!a.sink.empty()) add("sink", a.sink);
    return true;
}

// ---------------------------------------------------------------------------
// JSON e UDP
// ---------------------------------------------------------------------------
std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

// Mesmo formato que o nó Python publicava em /vision/yolo/detections.
std::string to_json(const std::vector<yolo::Detection>& dets, const yolo::ModelMeta& meta, long long stamp_ns) {
    std::string s = "[";
    char buf[256];
    for (size_t i = 0; i < dets.size(); ++i) {
        const auto& d = dets[i];
        std::snprintf(buf, sizeof(buf), "%s{\"x1\": %d, \"y1\": %d, \"x2\": %d, \"y2\": %d, \"conf\": %.4f, ",
                      i ? ", " : "", static_cast<int>(d.x1), static_cast<int>(d.y1), static_cast<int>(d.x2),
                      static_cast<int>(d.y2), d.conf);
        s += buf;
        s += "\"class\": \"" + json_escape(yolo::class_name(meta, d.class_id)) + "\", ";
        std::snprintf(buf, sizeof(buf), "\"class_id\": %d, \"stamp\": %lld}", d.class_id, stamp_ns);
        s += buf;
    }
    return s + "]";
}

class UdpSender {
public:
    bool open(const std::string& host_port) {
        std::string host;
        int port;
        if (!split_host_port(host_port, host, port)) return false;
        std::memset(&addr_, 0, sizeof(addr_));
        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, host.c_str(), &addr_.sin_addr) != 1) return false;
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        return fd_ >= 0;
    }
    void send(const std::string& msg) const {
        if (fd_ >= 0)
            sendto(fd_, msg.data(), msg.size(), 0, reinterpret_cast<const sockaddr*>(&addr_), sizeof(addr_));
    }
    ~UdpSender() { if (fd_ >= 0) close(fd_); }

private:
    int fd_ = -1;
    sockaddr_in addr_{};
};

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace

// ===========================================================================
int main(int argc, char** argv) {
    // O autovideosink testa vários sinks até achar um que funcione; o dfbvideosink
    // (DirectFB, do plugins-bad) pode derrubar o processo nessa sondagem quando não há
    // framebuffer. Tira ele da disputa, a menos que o usuário já tenha configurado ranks.
    setenv("GST_PLUGIN_FEATURE_RANK", "dfbvideosink:NONE", 0);
    gst_init(&argc, &argv);  // também consome as opções --gst-*

    Args a;
    if (!parse_args(argc, argv, a)) return 2;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const bool image_mode = !a.image.empty();
    std::vector<std::unique_ptr<OutputSlot>> outs;
    if (!build_outputs(a, image_mode, outs)) return 2;

    UdpSender udp;
    if (!a.udp.empty() && !udp.open(a.udp)) {
        std::fprintf(stderr, "endereço UDP inválido: %s (use IP:porta)\n", a.udp.c_str());
        return 2;
    }

    YoloNcnn::Options opt;
    opt.conf = a.conf;
    opt.iou = a.iou;
    opt.num_threads = a.threads;
    std::fprintf(stderr, "Carregando modelo: %s\n", a.model.c_str());
    std::unique_ptr<YoloNcnn> det;
    try {
        det = std::make_unique<YoloNcnn>(a.model, opt);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Erro: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr, "Modelo ok | entrada %dx%d | %zu classe(s) | conf=%.2f iou=%.2f threads=%d\n",
                 det->meta().imgsz_w, det->meta().imgsz_h, det->meta().names.size(), a.conf, a.iou, a.threads);

    bool live = true;
    const std::string desc = source_pipeline(a, live);
    std::fprintf(stderr, "Fonte: %s\n", desc.c_str());
    GstCapture cap;
    std::string err;
    if (!cap.open(desc, err)) {
        std::fprintf(stderr,
            "Não foi possível abrir a fonte: %s\n"
            "  - teste a câmera: rpicam-hello --list-cameras\n"
            "  - teste o GStreamer: gst-launch-1.0 libcamerasrc ! videoconvert ! fakesink\n"
            "  - confira os plugins: gstreamer1.0-libcamera e gstreamer1.0-plugins-base\n",
            err.c_str());
        return 1;
    }

    Frame f;
    double fps = 0.0, infer_ms_acc = 0.0;
    long frames = 0, frames_window = 0;
    int timeouts = 0, rc = 0;
    auto prev = std::chrono::steady_clock::now();
    auto last_log = prev;
    std::fprintf(stderr, "Detector iniciado (Ctrl+C para sair)\n");

    while (!g_stop) {
        const auto st = cap.read(f, 1000, err);
        if (st == GstCapture::Status::Timeout) {
            if (++timeouts >= 5) { std::fprintf(stderr, "Nenhum quadro há 5 s; encerrando.\n"); rc = 1; break; }
            continue;
        }
        if (st == GstCapture::Status::Eos) break;
        if (st == GstCapture::Status::Error) { std::fprintf(stderr, "Erro na captura: %s\n", err.c_str()); rc = 1; break; }
        timeouts = 0;

        // FPS (média móvel, como no nó Python)
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - prev).count();
        prev = now;
        if (frames > 0 && dt > 0) fps = (frames == 1) ? 1.0 / dt : 0.9 * fps + 0.1 / dt;

        std::vector<yolo::Detection> dets;
        try {
            if (image_mode) det->detect(f.rgb.data(), f.width, f.height, f.width * 3);  // aquecimento
            const auto t0 = std::chrono::steady_clock::now();
            dets = det->detect(f.rgb.data(), f.width, f.height, f.width * 3);
            infer_ms_acc += ms_since(t0);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Erro: %s\n", e.what());
            rc = 1;
            break;
        }
        ++frames;
        ++frames_window;

        // Sempre emite, mesmo vazio.
        const std::string js = to_json(dets, det->meta(), f.stamp_ns);
        if (!a.quiet) { std::fputs(js.c_str(), stdout); std::fputc('\n', stdout); std::fflush(stdout); }
        if (!a.udp.empty()) udp.send(js);

        // Saídas de vídeo: ao vivo, abrem após 10 quadros (para usar o FPS real no arquivo).
        bool any_open = false;
        for (auto& o : outs) {
            if (!o->failed && !o->out.is_open() && (!live || frames >= 10)) {
                const double out_fps = live ? fps : (f.nominal_fps > 0 ? f.nominal_fps : 30.0);
                if (o->out.open(o->tail, f.width, f.height, out_fps, live, err)) {
                    std::fprintf(stderr, "Saída aberta: %s (%.1f FPS)\n", o->name.c_str(), out_fps);
                } else {
                    std::fprintf(stderr, "[aviso] saída '%s' desabilitada: %s\n", o->name.c_str(), err.c_str());
                    o->failed = true;
                }
            }
            any_open = any_open || o->out.is_open();
        }
        if (any_open) {  // só desenha se alguém for ver
            draw::Image im{f.rgb.data(), f.width, f.height, f.width * 3};
            draw::detections(im, dets, det->meta(), fps, a.draw_fps && !image_mode);
            for (auto& o : outs) {
                if (o->out.is_open() && !o->out.push(f.rgb.data(), err)) {
                    std::fprintf(stderr, "[aviso] saída '%s' desabilitada: %s\n", o->name.c_str(), err.c_str());
                    o->out.close(0);
                    o->failed = true;
                }
            }
        }

        if (image_mode) {
            std::fprintf(stderr, "Inferência: %.1f ms | %zu detecção(ões)\n", infer_ms_acc, dets.size());
            break;
        }
        if (ms_since(last_log) > 2000.0) {
            std::fprintf(stderr, "FPS %.1f | inferência média %.1f ms | %zu detecção(ões)\n", fps,
                         infer_ms_acc / frames_window, dets.size());
            infer_ms_acc = 0.0;
            frames_window = 0;
            last_log = std::chrono::steady_clock::now();
        }
        if (a.max_frames > 0 && frames >= a.max_frames) break;
    }

    for (auto& o : outs) {
        if (o->out.is_open()) {
            o->out.close();  // EOS: fecha o arquivo direito
            if (image_mode) std::fprintf(stderr, "Imagem anotada salva em %s\n", o->name.c_str());
        }
    }
    cap.close();
    std::fprintf(stderr, "Encerrando após %ld quadro(s).\n", frames);
    return rc;
}
