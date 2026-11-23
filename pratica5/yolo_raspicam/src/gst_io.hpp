// Entrada e saída de vídeo com a API C do GStreamer (só o núcleo, gstreamer-1.0).
// appsink/appsrc são usados pelos sinais de ação do GObject ("try-pull-sample",
// "push-buffer", "end-of-stream"), então não é preciso o libgstreamer-plugins-base1.0-dev.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <gst/gst.h>

struct Frame {
    std::vector<uint8_t> rgb;  // RGB contíguo (stride = width * 3)
    int width = 0, height = 0;
    double nominal_fps = 0;    // framerate anunciado nas caps (0 se desconhecido)
    int64_t stamp_ns = 0;      // instante da CAPTURA, relógio do sistema (mesma base do tempo do ROS)
};

class GstCapture {
public:
    enum class Status { Ok, Timeout, Eos, Error };

    GstCapture() = default;
    GstCapture(const GstCapture&) = delete;
    GstCapture& operator=(const GstCapture&) = delete;
    ~GstCapture() { close(); }

    // A descrição precisa terminar num "appsink name=sink" com caps video/x-raw,format=RGB.
    bool open(const std::string& description, std::string& err);
    Status read(Frame& frame, int timeout_ms, std::string& err);
    void close();

private:
    GstElement* pipeline_ = nullptr;
    GstElement* sink_ = nullptr;
    GstBus* bus_ = nullptr;
};

class GstOutput {
public:
    GstOutput() = default;
    GstOutput(const GstOutput&) = delete;
    GstOutput& operator=(const GstOutput&) = delete;
    ~GstOutput() { close(); }

    // Monta "appsrc ! queue ! <tail>" para quadros RGB width x height.
    // live=true: carimbo de tempo pelo relógio e fila que descarta quadros velhos.
    // live=false (arquivo/foto): carimbo calculado a partir do fps, sem descartar quadros.
    bool open(const std::string& tail, int width, int height, double fps, bool live, std::string& err);
    // Envia um quadro RGB contíguo. Retorna false se o pipeline deu erro (ex.: sem display).
    bool push(const uint8_t* rgb, std::string& err);
    // Envia EOS e espera o pipeline terminar (necessário para fechar .avi/.mkv/.jpg corretamente).
    void close(int timeout_ms = 3000);
    bool is_open() const { return pipeline_ != nullptr; }

private:
    GstElement* pipeline_ = nullptr;
    GstElement* src_ = nullptr;
    GstBus* bus_ = nullptr;
    int width_ = 0, height_ = 0, stride_ = 0;
    bool live_ = true;
    GstClockTime frame_dur_ = 0;
    guint64 count_ = 0;
};
