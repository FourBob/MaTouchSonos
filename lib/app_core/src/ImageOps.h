#pragma once

#include <stddef.h>
#include <stdint.h>

namespace app {
namespace img {

/**
 * Bildoperationen für Albumcover – reines C++ auf RGB565-Puffern, auf dem PC getestet.
 * RGB565: 5 Bit Rot (oben), 6 Bit Grün, 5 Bit Blau (unten), wie LVGL/Display (little endian).
 */

enum class Format : uint8_t { Unknown, Jpeg, Png };

/** Erkennt das Bildformat an den ersten Bytes (JPEG: FF D8 FF, PNG: 89 50 4E 47). */
inline Format detectFormat(const uint8_t* data, size_t size) {
    if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return Format::Jpeg;
    if (size >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return Format::Png;
    return Format::Unknown;
}

/**
 * Größter JPEG-Verkleinerungsfaktor (1, 2, 4, 8), bei dem die kürzere Seite noch
 * mindestens `target` Pixel hat. Spart Speicher und Zeit bei großen Covern.
 */
inline int chooseJpegScale(int width, int height, int target) {
    const int shortSide = width < height ? width : height;
    int scale = 1;
    for (int s : {2, 4, 8}) {
        if (shortSide / s >= target) scale = s;
    }
    return scale;
}

inline uint16_t pack565(int r, int g, int b) {
    return static_cast<uint16_t>(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F));
}

/**
 * Skaliert `src` so, dass `dst` vollständig bedeckt ist („cover“), und schneidet mittig zu.
 * Bilineare Interpolation – einmal pro Cover, danach kostet das Anzeigen nichts mehr.
 */
inline void coverResize(const uint16_t* src, int sw, int sh, uint16_t* dst, int dw, int dh) {
    if (sw <= 0 || sh <= 0) return;
    // Ausschnitt der Quelle mit dem Seitenverhältnis des Ziels, mittig
    int cropW = sw, cropH = sh;
    if (static_cast<int64_t>(sw) * dh > static_cast<int64_t>(sh) * dw) {
        cropW = static_cast<int>(static_cast<int64_t>(sh) * dw / dh);
    } else {
        cropH = static_cast<int>(static_cast<int64_t>(sw) * dh / dw);
    }
    const int offX = (sw - cropW) / 2;
    const int offY = (sh - cropH) / 2;

    for (int y = 0; y < dh; ++y) {
        // Festkomma 16.16: Pixelmitte des Ziels auf die Quelle abbilden
        const int32_t fy = static_cast<int32_t>(((static_cast<int64_t>(y) * 2 + 1) * cropH << 15) / dh) - 32768;
        int y0 = fy < 0 ? 0 : (fy >> 16);
        if (y0 > cropH - 1) y0 = cropH - 1;
        const int y1 = y0 + 1 < cropH ? y0 + 1 : cropH - 1;
        const int wy = fy < 0 ? 0 : ((fy >> 8) & 0xFF);
        const uint16_t* row0 = src + static_cast<size_t>(offY + y0) * sw + offX;
        const uint16_t* row1 = src + static_cast<size_t>(offY + y1) * sw + offX;
        uint16_t* out = dst + static_cast<size_t>(y) * dw;

        for (int x = 0; x < dw; ++x) {
            const int32_t fx = static_cast<int32_t>(((static_cast<int64_t>(x) * 2 + 1) * cropW << 15) / dw) - 32768;
            int x0 = fx < 0 ? 0 : (fx >> 16);
            if (x0 > cropW - 1) x0 = cropW - 1;
            const int x1 = x0 + 1 < cropW ? x0 + 1 : cropW - 1;
            const int wx = fx < 0 ? 0 : ((fx >> 8) & 0xFF);

            const uint16_t p00 = row0[x0], p01 = row0[x1], p10 = row1[x0], p11 = row1[x1];
            auto lerp2 = [&](int shift, int mask) {
                const int a = (p00 >> shift) & mask, b = (p01 >> shift) & mask;
                const int c = (p10 >> shift) & mask, d = (p11 >> shift) & mask;
                const int top = a * (256 - wx) + b * wx;
                const int bottom = c * (256 - wx) + d * wx;
                return (top * (256 - wy) + bottom * wy + (1 << 15)) >> 16;
            };
            out[x] = pack565(lerp2(11, 0x1F), lerp2(5, 0x3F), lerp2(0, 0x1F));
        }
    }
}

/** Dunkelt alle Pixel ab: factor/256 (z. B. 110 ≈ 43 % Helligkeit), damit Text lesbar bleibt. */
inline void darken(uint16_t* buf, size_t count, uint16_t factor) {
    for (size_t i = 0; i < count; ++i) {
        const uint16_t p = buf[i];
        const int r = (((p >> 11) & 0x1F) * factor) >> 8;
        const int g = (((p >> 5) & 0x3F) * factor) >> 8;
        const int b = ((p & 0x1F) * factor) >> 8;
        buf[i] = pack565(r, g, b);
    }
}

}  // namespace img
}  // namespace app
