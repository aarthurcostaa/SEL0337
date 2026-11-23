// Pré/pós-processamento do YOLO (Ultralytics) sem dependências externas.
// Fica separado de ncnn/OpenCV para poder ser testado em qualquer máquina.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace yolo {

struct Detection {
    float x1, y1, x2, y2;  // pixels da imagem ORIGINAL
    float conf;
    int class_id;
};

// Letterbox igual ao do Ultralytics (LetterBox, center=True):
// redimensiona mantendo proporção e preenche o resto com cinza 114.
struct Letterbox {
    float scale = 1.f;
    int new_w = 0, new_h = 0;               // tamanho após o resize
    int top = 0, bottom = 0, left = 0, right = 0;  // bordas
};

inline Letterbox make_letterbox(int src_w, int src_h, int dst_w, int dst_h) {
    Letterbox lb;
    lb.scale = std::min(static_cast<float>(dst_w) / src_w, static_cast<float>(dst_h) / src_h);
    lb.new_w = static_cast<int>(std::lround(src_w * lb.scale));
    lb.new_h = static_cast<int>(std::lround(src_h * lb.scale));
    const float dw = (dst_w - lb.new_w) / 2.f;
    const float dh = (dst_h - lb.new_h) / 2.f;
    lb.top = static_cast<int>(std::lround(dh - 0.1f));
    lb.bottom = static_cast<int>(std::lround(dh + 0.1f));
    lb.left = static_cast<int>(std::lround(dw - 0.1f));
    lb.right = static_cast<int>(std::lround(dw + 0.1f));
    // garante que a soma feche exatamente no tamanho da rede
    lb.bottom = dst_h - lb.new_h - lb.top;
    lb.right = dst_w - lb.new_w - lb.left;
    return lb;
}

inline float iou(const Detection& a, const Detection& b) {
    const float ix1 = std::max(a.x1, b.x1), iy1 = std::max(a.y1, b.y1);
    const float ix2 = std::min(a.x2, b.x2), iy2 = std::min(a.y2, b.y2);
    const float inter = std::max(0.f, ix2 - ix1) * std::max(0.f, iy2 - iy1);
    const float uni = (a.x2 - a.x1) * (a.y2 - a.y1) + (b.x2 - b.x1) * (b.y2 - b.y1) - inter;
    return uni > 0.f ? inter / uni : 0.f;
}

// NMS por classe (igual ao padrão do Ultralytics, agnostic=False).
inline std::vector<Detection> nms(std::vector<Detection> dets, float iou_thr, int max_det,
                                  bool agnostic = false) {
    std::sort(dets.begin(), dets.end(),
              [](const Detection& a, const Detection& b) { return a.conf > b.conf; });
    std::vector<Detection> keep;
    std::vector<bool> removed(dets.size(), false);
    for (size_t i = 0; i < dets.size() && static_cast<int>(keep.size()) < max_det; ++i) {
        if (removed[i]) continue;
        keep.push_back(dets[i]);
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (removed[j]) continue;
            if (!agnostic && dets[j].class_id != dets[i].class_id) continue;
            if (iou(dets[i], dets[j]) > iou_thr) removed[j] = true;
        }
    }
    return keep;
}

// Decodifica a saída "out0" do YOLOv8/YOLO11 exportado (end2end=false):
//   rows[0..3][i] = cx, cy, w, h  (pixels da entrada da rede, ex. 320x320)
//   rows[4+k][i]  = score da classe k (já passou por sigmoid)
// num_anchors = 2100 para 320x320 (40*40 + 20*20 + 10*10).
// Retorna caixas já convertidas para a imagem original (desfaz o letterbox),
// após limiar de confiança e NMS.
inline std::vector<Detection> decode(const float* const* rows, int num_rows, int num_anchors,
                                     const Letterbox& lb, int src_w, int src_h,
                                     float conf_thr, float iou_thr, int max_det = 300,
                                     bool agnostic = false) {
    const int num_classes = num_rows - 4;
    std::vector<Detection> cand;
    if (num_classes <= 0) return cand;

    for (int i = 0; i < num_anchors; ++i) {
        int best = 0;
        float best_score = rows[4][i];
        for (int k = 1; k < num_classes; ++k) {
            const float s = rows[4 + k][i];
            if (s > best_score) { best_score = s; best = k; }
        }
        if (!(best_score > conf_thr)) continue;

        const float cx = rows[0][i], cy = rows[1][i], w = rows[2][i], h = rows[3][i];
        Detection d;
        d.x1 = (cx - 0.5f * w - lb.left) / lb.scale;
        d.y1 = (cy - 0.5f * h - lb.top) / lb.scale;
        d.x2 = (cx + 0.5f * w - lb.left) / lb.scale;
        d.y2 = (cy + 0.5f * h - lb.top) / lb.scale;
        d.x1 = std::clamp(d.x1, 0.f, static_cast<float>(src_w));
        d.y1 = std::clamp(d.y1, 0.f, static_cast<float>(src_h));
        d.x2 = std::clamp(d.x2, 0.f, static_cast<float>(src_w));
        d.y2 = std::clamp(d.y2, 0.f, static_cast<float>(src_h));
        d.conf = best_score;
        d.class_id = best;
        cand.push_back(d);
    }
    return nms(std::move(cand), iou_thr, max_det, agnostic);
}

// ---------------------------------------------------------------------------
// metadata.yaml gerado pelo export do Ultralytics. Só lemos o necessário
// (imgsz e names) com um parser mínimo, para não depender de yaml-cpp.
// ---------------------------------------------------------------------------
struct ModelMeta {
    int imgsz_w = 320, imgsz_h = 320;
    std::vector<std::string> names;  // índice = class_id
};

namespace detail {
inline std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
inline std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"') && s.back() == s.front())
        s = s.substr(1, s.size() - 2);
    return s;
}
}  // namespace detail

// Retorna false se o arquivo não existir (mantém os padrões).
inline bool load_metadata(const std::string& path, ModelMeta& meta) {
    std::ifstream f(path);
    if (!f) return false;

    std::string line, section;
    std::vector<int> imgsz;
    std::vector<std::pair<int, std::string>> names;
    while (std::getline(f, line)) {
        if (detail::trim(line).empty() || detail::trim(line)[0] == '#') continue;
        const bool indented = line[0] == ' ' || line[0] == '\t' || line[0] == '-';
        const std::string t = detail::trim(line);
        if (!indented) {
            const auto colon = t.find(':');
            section = detail::trim(t.substr(0, colon));
            const std::string value = colon == std::string::npos ? "" : detail::trim(t.substr(colon + 1));
            if (section == "imgsz" && !value.empty()) {  // forma "imgsz: [320, 320]" ou "imgsz: 320"
                std::string v = value;
                for (char& c : v) if (c == '[' || c == ']' || c == ',') c = ' ';
                std::istringstream ss(v);
                int n;
                while (ss >> n) imgsz.push_back(n);
            }
            continue;
        }
        if (section == "imgsz" && t.rfind("- ", 0) == 0) {
            imgsz.push_back(std::atoi(t.c_str() + 2));
        } else if (section == "names") {
            const auto colon = t.find(':');
            if (colon == std::string::npos) continue;
            names.emplace_back(std::atoi(t.substr(0, colon).c_str()), detail::unquote(t.substr(colon + 1)));
        }
    }

    if (imgsz.size() == 1) imgsz.push_back(imgsz[0]);
    if (imgsz.size() >= 2) { meta.imgsz_h = imgsz[0]; meta.imgsz_w = imgsz[1]; }  // Ultralytics: [h, w]
    if (!names.empty()) {
        int max_id = 0;
        for (auto& p : names) max_id = std::max(max_id, p.first);
        meta.names.assign(max_id + 1, "");
        for (auto& p : names) meta.names[p.first] = p.second;
    }
    return true;
}

inline std::string class_name(const ModelMeta& meta, int id) {
    if (id >= 0 && id < static_cast<int>(meta.names.size()) && !meta.names[id].empty())
        return meta.names[id];
    return std::to_string(id);
}

}  // namespace yolo
