"""Gera src/font.hpp: fonte bitmap proporcional ASCII 32..126, rasterizada da Hershey Simplex (domínio público).

Uso: python3 gen_font.py <saida.hpp> [escala]
"""
import sys

import cv2
import numpy as np

SCALE = float(sys.argv[2]) if len(sys.argv) > 2 else 0.4
FONT = cv2.FONT_HERSHEY_SIMPLEX
out_path = sys.argv[1]
chars = [chr(c) for c in range(32, 127)]

# Renderiza cada glifo numa tela grande, com a mesma linha de base
CW, CH, BASE_Y, X0 = 64, 64, 40, 8
canvases = []
for c in chars:
    img = np.zeros((CH, CW), np.uint8)
    cv2.putText(img, c, (X0, BASE_Y), FONT, SCALE, 255, 1, cv2.LINE_8)
    canvases.append(img > 100)

# Corte vertical comum a todos (para alinhar a linha de base)
ys = np.where(np.any(np.stack(canvases), axis=(0, 2)))[0]
y0, y1 = ys.min(), ys.max() + 1
H = int(y1 - y0)

space_w = cv2.getTextSize('n', FONT, SCALE, 1)[0][0] // 2 + 1
glyphs = []
for c, cv in zip(chars, canvases):
    cols = np.where(cv.any(axis=0))[0]
    if len(cols) == 0:
        glyphs.append((space_w, [0] * H))
        continue
    x0, x1 = cols.min(), cols.max() + 1
    w = int(x1 - x0)
    rows = []
    for y in range(y0, y1):
        bits = 0
        for x in range(x0, x1):
            if cv[y, x]:
                bits |= 1 << (w - 1 - (x - x0))
        rows.append(bits)
    glyphs.append((w, rows))

W = max(w for w, _ in glyphs)
assert W <= 16, W
with open(out_path, 'w') as f:
    f.write('// Gerado por tools/gen_font.py: fonte Hershey Simplex (domínio público) rasterizada,\n')
    f.write('// proporcional. ASCII 32..126; cada glifo tem largura WIDTH[i] e FONT_H linhas, em que o\n')
    f.write('// bit (WIDTH[i]-1-x) da linha y é o pixel (x, y).\n')
    f.write('#pragma once\n#include <cstdint>\n\nnamespace font {\n')
    f.write(f'constexpr int FONT_H = {H};\nconstexpr int SPACING = 1;  // pixels entre glifos\n')
    f.write('constexpr uint8_t WIDTH[95] = {' + ','.join(str(w) for w, _ in glyphs) + '};\n')
    f.write('constexpr uint16_t GLYPHS[95][FONT_H] = {\n')
    for c, (w, rows) in zip(chars, glyphs):
        f.write('    {' + ','.join(f'0x{r:04x}' for r in rows) + f'}},  // {c!r}\n')
    f.write('};\n}  // namespace font\n')
print('H', H, 'Wmax', W)
