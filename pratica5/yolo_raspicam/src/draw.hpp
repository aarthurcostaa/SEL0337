// Desenho simples em imagem RGB 8 bits (caixas, retângulos cheios e texto bitmap),
// para não depender do OpenCV só por causa do frame anotado.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "font.hpp"
#include "postprocess.hpp"

namespace draw {

struct Rgb { uint8_t r, g, b; };

struct Image {  // não é dona dos dados
    uint8_t* data;
    int width, height, stride;  // stride em bytes
};

inline void fill_rect(Image& im, int x0, int y0, int x1, int y1, Rgb c) {  // [x0,x1) x [y0,y1)
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, im.width); y1 = std::min(y1, im.height);
    for (int y = y0; y < y1; ++y) {
        uint8_t* p = im.data + static_cast<size_t>(y) * im.stride + static_cast<size_t>(x0) * 3;
        for (int x = x0; x < x1; ++x, p += 3) { p[0] = c.r; p[1] = c.g; p[2] = c.b; }
    }
}

inline void draw_rect(Image& im, int x0, int y0, int x1, int y1, Rgb c, int t) {  // borda por dentro
    fill_rect(im, x0, y0, x1, y0 + t, c);
    fill_rect(im, x0, y1 - t, x1, y1, c);
    fill_rect(im, x0, y0, x0 + t, y1, c);
    fill_rect(im, x1 - t, y0, x1, y1, c);
}

inline int glyph_index(char ch) {
    const int i = static_cast<unsigned char>(ch) - 32;
    return (i >= 0 && i < 95) ? i : ('?' - 32);
}

inline int text_width(const std::string& s, int scale) {
    int w = 0;
    for (char ch : s) w += (font::WIDTH[glyph_index(ch)] + font::SPACING) * scale;
    return w;
}

inline int text_height(int scale) { return font::FONT_H * scale; }

inline void draw_text(Image& im, int x, int y_top, const std::string& s, Rgb c, int scale) {
    for (char ch : s) {
        const int gi = glyph_index(ch);
        const int w = font::WIDTH[gi];
        for (int gy = 0; gy < font::FONT_H; ++gy) {
            const uint16_t row = font::GLYPHS[gi][gy];
            if (!row) continue;
            for (int gx = 0; gx < w; ++gx)
                if (row >> (w - 1 - gx) & 1)
                    fill_rect(im, x + gx * scale, y_top + gy * scale, x + (gx + 1) * scale, y_top + (gy + 1) * scale, c);
        }
        x += (w + font::SPACING) * scale;
    }
}

// Caixas + rótulo "classe conf" + FPS, no estilo do results[0].plot() do nó Python.
inline void detections(Image& im, const std::vector<yolo::Detection>& dets, const yolo::ModelMeta& meta,
                       double fps, bool draw_fps) {
    static const Rgb palette[] = {{255, 56, 56}, {255, 157, 151}, {255, 112, 31},
                                  {255, 178, 29}, {207, 210, 49}, {72, 249, 10}};
    const int s = im.width >= 1280 ? 2 : 1;           // escala do texto
    const int t = std::max(2, im.width / 320);        // espessura da caixa
    for (const auto& d : dets) {
        const Rgb c = palette[d.class_id % 6];
        const int x0 = static_cast<int>(d.x1), y0 = static_cast<int>(d.y1);
        const int x1 = static_cast<int>(d.x2), y1 = static_cast<int>(d.y2);
        draw_rect(im, x0, y0, x1, y1, c, t);

        char label[96];
        std::snprintf(label, sizeof(label), "%s %.2f", yolo::class_name(meta, d.class_id).c_str(), d.conf);
        const int tw = text_width(label, s) + 4, th = text_height(s) + 4;
        const int ly = y0 - th >= 0 ? y0 - th : y0;   // acima da caixa; dentro se não couber
        fill_rect(im, x0, ly, x0 + tw, ly + th, c);
        draw_text(im, x0 + 2, ly + 2, label, {255, 255, 255}, s);
    }
    if (draw_fps) {
        char txt[32];
        std::snprintf(txt, sizeof(txt), "FPS: %.1f", fps);
        const int fs = 2 * s;
        fill_rect(im, 6, 6, 6 + text_width(txt, fs) + 8, 6 + text_height(fs) + 8, {0, 0, 0});
        draw_text(im, 10, 10, txt, {0, 255, 0}, fs);
    }
}

}  // namespace draw
