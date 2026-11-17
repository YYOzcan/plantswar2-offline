// TextToImage (Android Paint/Canvas) reimplemented with stb_truetype. Mirrors the Java algorithm in
// com/ideabox/Library/TextToImage.java: same line breaking, sizes (+15 width, size+3 per line) and
// output format (RGBA, white, premultiplied alpha).
#include "rt.h"
#include "platform.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"
#include <cmath>

namespace {
struct Font { std::vector<uint8_t> data; stbtt_fontinfo info; bool ok = false; };
Font fonts[2];
int cur = 0;

std::u32string utf8(const std::string &s) {
    std::u32string r;
    for (size_t i = 0; i < s.size();) {
        unsigned char ch = s[i]; char32_t cp; int n;
        if (ch < 0x80) { cp = ch; n = 1; }
        else if ((ch >> 5) == 6) { cp = ch & 0x1f; n = 2; }
        else if ((ch >> 4) == 14) { cp = ch & 0x0f; n = 3; }
        else { cp = ch & 0x07; n = 4; }
        for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3f);
        r += cp; i += n;
    }
    return r;
}

struct Paint {
    Font *f; float size, scale; bool bold;
    Paint(int sz, int b) : f(&fonts[cur]), size((float)sz), bold(b == 1) {
        if (!f->ok) f = &fonts[0];
        scale = stbtt_ScaleForMappingEmToPixels(&f->info, size);
    }
    Font *font_for(char32_t c) {
        if (stbtt_FindGlyphIndex(&f->info, c)) return f;
        for (auto &o : fonts) if (o.ok && stbtt_FindGlyphIndex(&o.info, c)) return &o;
        return f;
    }
    float sc(Font *g) { return g == f ? scale : stbtt_ScaleForMappingEmToPixels(&g->info, size); }
    float advance(char32_t c) {
        Font *g = font_for(c); int adv, lsb; stbtt_GetCodepointHMetrics(&g->info, c, &adv, &lsb);
        return adv * sc(g) + (bold ? size / 24.f : 0);
    }
    float ascent() { int a, d, l; stbtt_GetFontVMetrics(&f->info, &a, &d, &l); return -a * scale; }   // negative, like Paint
    float measure(const std::u32string &s) { float w = 0; for (char32_t c : s) w += advance(c); return w; }
    // Paint.getTextBounds: tight ink bounds relative to (0, baseline)
    void bounds(const std::u32string &s, int &l, int &t, int &r, int &b) {
        float x = 0; bool any = false; l = t = r = b = 0;
        for (char32_t c : s) {
            Font *g = font_for(c); int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&g->info, c, sc(g), sc(g), &x0, &y0, &x1, &y1);
            if (x1 > x0 && y1 > y0) {
                int gl = (int)floorf(x + x0), gr = (int)ceilf(x + x1) + (bold ? 1 : 0);
                if (!any) { l = gl; r = gr; t = y0; b = y1; any = true; }
                else { l = std::min(l, gl); r = std::max(r, gr); t = std::min(t, y0); b = std::max(b, y1); }
            }
            x += advance(c);
        }
        if (!any) { l = 0; r = (int)ceilf(x); }
    }
    int break_text(const std::u32string &s, float width) {
        float w = 0; int n = 0;
        for (char32_t c : s) { float a = advance(c); if (w + a > width) break; w += a; n++; }
        return n;
    }
    // Canvas.drawText(s, x, baseline) with the given alignment (-1 left, 0 center, 1 right)
    void draw(std::vector<uint8_t> &px, int W, int H, const std::u32string &s, float x, float base, int align) {
        float w = measure(s);
        if (align == 0) x -= w / 2; else if (align == 1) x -= w;
        for (char32_t c : s) {
            Font *g = font_for(c); float gs = sc(g);
            int x0, y0, x1, y1; float fx = x - floorf(x);
            stbtt_GetCodepointBitmapBoxSubpixel(&g->info, c, gs, gs, fx, 0, &x0, &y0, &x1, &y1);
            int gw = x1 - x0, gh = y1 - y0;
            if (gw > 0 && gh > 0) {
                std::vector<uint8_t> gb(gw * gh);
                stbtt_MakeCodepointBitmapSubpixel(&g->info, gb.data(), gw, gh, gw, gs, gs, fx, 0, c);
                int ox = (int)floorf(x) + x0, oy = (int)lroundf(base) + y0;
                for (int yy = 0; yy < gh; yy++)
                    for (int xx = 0; xx < gw; xx++) {
                        int a = gb[yy * gw + xx]; if (!a) continue;
                        for (int dx = 0; dx <= (bold ? 1 : 0); dx++) {
                            int X = ox + xx + dx, Y = oy + yy;
                            if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                            uint8_t *p = &px[(Y * W + X) * 4];
                            int v = std::min(255, p[3] + a - p[3] * a / 255);
                            p[0] = p[1] = p[2] = p[3] = (uint8_t)v;
                        }
                    }
            }
            x += advance(c);
        }
    }
};

std::string utf8_of(const std::u32string &s) {
    std::string r;
    for (char32_t c : s) {
        if (c < 0x80) r += (char)c;
        else if (c < 0x800) { r += (char)(0xC0 | (c >> 6)); r += (char)(0x80 | (c & 0x3f)); }
        else if (c < 0x10000) { r += (char)(0xE0 | (c >> 12)); r += (char)(0x80 | ((c >> 6) & 0x3f)); r += (char)(0x80 | (c & 0x3f)); }
        else { r += (char)(0xF0 | (c >> 18)); r += (char)(0x80 | ((c >> 12) & 0x3f)); r += (char)(0x80 | ((c >> 6) & 0x3f)); r += (char)(0x80 | (c & 0x3f)); }
    }
    return r;
}
}  // namespace

void plat::text_init() {
    const char *files[2] = {"assets/Common/nanum_b.ttf", "assets/Common/THSarabunNew-Bold.ttf"};
    for (int k = 0; k < 2; k++) {
        if (!apk_read(files[k], fonts[k].data)) { rt::logf("font %s missing", files[k]); continue; }
        fonts[k].ok = stbtt_InitFont(&fonts[k].info, fonts[k].data.data(), stbtt_GetFontOffsetForIndex(fonts[k].data.data(), 0)) != 0;
    }
}
void plat::text_set_font(int type) { cur = type; }

std::vector<uint8_t> plat::text_string(const std::string &in, int size, int width, int bold, int wordBreak, int align,
                                       int multiLine, int &W, int &H) {
    Paint paint(size, bold);
    std::u32string str = utf8(in);
    std::vector<std::u32string> vec;
    W = 0; H = 0;
    if (width > 0 && multiLine > 1) {
        size_t count = 0;
        do {
            float w = 0; size_t len = 0, temp = 0; bool newLine = false;
            while (count + temp < str.size() && w < width - paint.advance(str[count + temp])) {
                w += paint.advance(str[count + temp]); w = (float)(int)w;
                temp++;
                if (count + temp >= str.size()) { len = temp; break; }
                if (str[count + temp] == ' ') len = temp;
                if (str[count + temp] == '\n') { len = temp; temp++; newLine = true; break; }
            }
            if (!newLine) { if (len == 0) len = temp; if (wordBreak == 1) len = temp; }
            if (len == 0) len = 1;
            std::u32string sub = str.substr(count, len);
            int l, t, r, b; paint.bounds(sub, l, t, r, b);
            W = std::max(W, r - l);
            H += size + 3;
            vec.push_back(sub);
            count += len;
            if (newLine) count++;
            if (count >= str.size()) break;
            if (str[count] == ' ') count++;
        } while (count < str.size());
    } else if (width <= 0) {
        int l, t, r, b; paint.bounds(str, l, t, r, b);
        vec.push_back(str); W = r - l; H = size + 3;
    } else {
        std::u32string dots = U"...";
        if (paint.break_text(str, (float)width) < (int)str.size()) {
            int dl, dt, dr, db; paint.bounds(dots, dl, dt, dr, db);
            int n = paint.break_text(str, (float)(width - (dr - dl)));
            str = str.substr(0, n) + dots;
        }
        int l, t, r, b; paint.bounds(str, l, t, r, b);
        vec.push_back(str); W = r - l; H = size + 3;
    }
    W += 15;
    if (H <= 0) H = 1;
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    float x = align == 0 ? W / 2.f : align == 1 ? (float)W : 0.f;
    float top = 0;
    for (auto &line : vec) { paint.draw(px, W, H, line, x, top - paint.ascent(), align); top += size + 2; }
    return px;
}

std::vector<uint8_t> plat::text_char(const std::string &in, int size, int, int bold, int &W, int &H) {
    Paint paint(size, bold);
    std::u32string s = utf8(in);
    int l, t, r, b; paint.bounds(s, l, t, r, b);
    W = (r - l) + 2; H = size + 5;
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    paint.draw(px, W, H, s, (float)-l, -paint.ascent(), -1);
    return px;
}

int plat::text_char_width(const std::string &in, int size, int bold) {
    Paint paint(size, bold);
    int l, t, r, b; paint.bounds(utf8(in), l, t, r, b);
    return (r - l) + 2;
}
