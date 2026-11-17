// SDL2 host: window + GL context, input -> touch, audio mixer, text entry, APK reader, game lifecycle.
#include "rt.h"
#include "platform.h"
#include "dyn.h"
#include "host.h"
#include "server.h"
#include <zlib.h>
#include <mutex>
#include <algorithm>
#include <map>
#include <cmath>
#include <atomic>

#define DR_WAV_IMPLEMENTATION
#include "../third_party/dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "../third_party/dr_mp3.h"

namespace rt {
void jni_init();
uint32_t jni_env();
uint32_t jni_new_string(const std::string &s);
uint32_t jni_instance(const char *cls);
uint32_t jni_native(const char *cls, const char *method, uint32_t thiz, std::initializer_list<uint32_t> args);
std::string host_path(const char *p) {        // relative guest paths (cwd = "/" on Android) go to the data dir
    if (!p || !*p) return std::string();
    std::string s = p;
    bool abs = s[0] == '/' || s[0] == '\\' || (s.size() > 1 && s[1] == ':');
    return abs ? s : data_dir + "/" + s;
}
}  // namespace rt
using namespace rt;

// ======================================================================= APK (zip) reader
namespace {
struct ZipEntry { uint32_t off, csize, usize; uint16_t method; };
std::vector<uint8_t> apk;
std::map<std::string, ZipEntry> zip;
uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
}  // namespace

// Single-file builds: the game data (APK, a zip) is appended to the executable with its offsets
// adjusted (zip -A), so the executable itself is a valid APK. Extra runtime files (SDL2.dll) live inside it.
bool plat::pack_extract(const std::string &name, const std::string &dest) {
    std::vector<uint8_t> b; if (!apk_read(name, b)) return false;
    bool d; uint64_t cur; int64_t mt;
    if (host::stat(dest, d, cur, mt) && cur == b.size()) return true;          // already there
    std::string tmp = dest + ".tmp";
    FILE *f = host::fopen(tmp, "wb"); if (!f) return false;
    bool ok = fwrite(b.data(), 1, b.size(), f) == b.size(); fclose(f);
    return ok && host::rename(tmp, dest) == 0;
}
bool plat::apk_open(const std::string &path) {
    apk.clear(); zip.clear();
    {
        FILE *f = host::fopen(path, "rb"); if (!f) return false;
        host::fseek64(f, 0, SEEK_END); int64_t sz = host::ftell64(f); host::fseek64(f, 0, SEEK_SET);
        apk.resize((size_t)sz); if (fread(apk.data(), 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return false; } fclose(f);
    }
    long n = (long)apk.size();
    long e = n - 22; while (e > 0 && le32(&apk[e]) != 0x06054b50) e--;
    if (e <= 0) return false;
    uint32_t cd = le32(&apk[e + 16]), cnt = le16(&apk[e + 10]);
    for (uint32_t k = 0, p = cd; k < cnt; k++) {
        const uint8_t *h = &apk[p];
        uint16_t nl = le16(h + 28), el = le16(h + 30), cl = le16(h + 32);
        std::string name((const char *)h + 46, nl);
        uint32_t lo = le32(h + 42);
        uint32_t data = lo + 30 + le16(&apk[lo + 26]) + le16(&apk[lo + 28]);
        zip[name] = ZipEntry{data, le32(h + 20), le32(h + 24), le16(h + 10)};
        p += 46 + nl + el + cl;
    }
    return true;
}

bool plat::apk_read(const std::string &entry, std::vector<uint8_t> &out) {
    auto it = zip.find(entry);
    if (it == zip.end()) return false;
    const ZipEntry &z = it->second;
    out.resize(z.usize);
    if (z.method == 0) { memcpy(out.data(), &apk[z.off], z.usize); return true; }
    z_stream s{}; inflateInit2(&s, -MAX_WBITS);
    s.next_in = &apk[z.off]; s.avail_in = z.csize; s.next_out = out.data(); s.avail_out = z.usize;
    int r = inflate(&s, Z_FINISH); inflateEnd(&s);
    return r == Z_STREAM_END;
}

// ======================================================================= audio
namespace {
struct Pcm { std::vector<float> data; };            // stereo, 44100 Hz
struct Voice { Pcm *pcm; size_t pos; bool loop; bool paused; int id; };
std::mutex amu;
std::vector<Pcm *> effects;
Pcm *music_pcm; size_t music_pos; bool music_on, music_loop, music_paused;
std::vector<Voice> voices;
float music_vol = 1.0f, effect_vol = 1.0f;
std::string music_path;
std::atomic<bool> music_ended{false};

Pcm *decode(const std::string &path) {
    std::vector<uint8_t> b;
    if (!plat::apk_read(path, b)) { logf("sound %s not found", path.c_str()); return nullptr; }
    unsigned ch = 0, rate = 0; uint64_t frames = 0; float *pcm = nullptr;
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".mp3") == 0) {
        drmp3_config cfg{}; drmp3_uint64 fr = 0;
        pcm = drmp3_open_memory_and_read_pcm_frames_f32(b.data(), b.size(), &cfg, &fr, nullptr);
        ch = cfg.channels; rate = cfg.sampleRate; frames = fr;
    } else {
        drwav_uint64 fr = 0;
        pcm = drwav_open_memory_and_read_pcm_frames_f32(b.data(), b.size(), &ch, &rate, &fr, nullptr);
        frames = fr;
    }
    if (!pcm) { logf("cannot decode %s", path.c_str()); return nullptr; }
    Pcm *p = new Pcm();
    double step = rate / 44100.0; uint64_t out_frames = (uint64_t)(frames / step);
    p->data.resize(out_frames * 2);
    for (uint64_t k = 0; k < out_frames; k++) {
        uint64_t s = (uint64_t)(k * step); if (s >= frames) s = frames - 1;
        float l = pcm[s * ch], r = ch > 1 ? pcm[s * ch + 1] : l;
        p->data[2 * k] = l; p->data[2 * k + 1] = r;
    }
    free(pcm);
    return p;
}

void audio_cb(void *, Uint8 *stream, int len) {
    float *out = (float *)stream; int n = len / (int)sizeof(float);
    memset(out, 0, len);
    std::lock_guard<std::mutex> l(amu);
    if (music_on && !music_paused && music_pcm) {
        for (int k = 0; k < n; k++) {
            if (music_pos >= music_pcm->data.size()) {
                if (music_loop) music_pos = 0; else { music_on = false; music_ended = true; break; }
            }
            out[k] += music_pcm->data[music_pos++] * music_vol;
        }
    }
    for (auto &v : voices) {
        if (v.paused || !v.pcm) continue;
        for (int k = 0; k < n; k++) {
            if (v.pos >= v.pcm->data.size()) { if (v.loop) v.pos = 0; else { v.pcm = nullptr; break; } }
            out[k] += v.pcm->data[v.pos++] * effect_vol;
        }
    }
    voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice &v) { return !v.pcm; }), voices.end());
    for (int k = 0; k < n; k++) out[k] = out[k] > 1.f ? 1.f : out[k] < -1.f ? -1.f : out[k];
}
}  // namespace

void plat::audio_init() {
    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev) { logf("no audio: %s", SDL_GetError()); return; }
    SDL_PauseAudioDevice(dev, 0);
}
void plat::music_load(const std::string &path) { music_path = path; }
void plat::music_play(int loop) {
    Pcm *p = decode(music_path);
    std::lock_guard<std::mutex> l(amu);
    delete music_pcm; music_pcm = p; music_pos = 0; music_on = p != nullptr; music_loop = loop != 0; music_paused = false;
}
void plat::music_stop() { std::lock_guard<std::mutex> l(amu); music_on = false; }
void plat::music_pause() { std::lock_guard<std::mutex> l(amu); music_paused = true; }
void plat::music_resume() { std::lock_guard<std::mutex> l(amu); music_paused = false; }
void plat::music_volume(float v) { std::lock_guard<std::mutex> l(amu); music_vol = v; }
int plat::effect_load(const std::string &path) {
    Pcm *p = decode(path);
    std::lock_guard<std::mutex> l(amu); effects.push_back(p); return (int)effects.size() - 1;
}
void plat::effect_play(int id, int loop) {
    std::lock_guard<std::mutex> l(amu);
    if (id < 0 || id >= (int)effects.size() || !effects[id]) return;
    if (loop == -1) for (auto &v : voices) if (v.id == id && v.loop) { v.paused = false; return; }
    voices.push_back(Voice{effects[id], 0, loop == -1, false, id});
}
void plat::effect_stop(int id) { std::lock_guard<std::mutex> l(amu); for (auto &v : voices) if (v.id == id) v.paused = true; }
void plat::effect_pause_all() { std::lock_guard<std::mutex> l(amu); for (auto &v : voices) v.paused = true; }
void plat::effect_resume_all() { std::lock_guard<std::mutex> l(amu); for (auto &v : voices) v.paused = false; }
void plat::effect_volume(float v) { std::lock_guard<std::mutex> l(amu); effect_vol = v; }

// ======================================================================= keyboard overlay
namespace {
bool kb_active; std::string kb_text; int kb_max = 32;
}
void plat::keyboard_show(const std::string &text, int) { kb_active = true; kb_text = text; SDL_StartTextInput(); }

static void draw_keyboard_overlay(int w, int h) {
    if (!kb_active) return;
    int tw, th;
    std::vector<uint8_t> px = plat::text_string(kb_text + "_", 40, 0, 1, 0, -1, 1, tw, th);
    GLint old_tex = 0, old_mode = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_tex); glGetIntegerv(GL_MATRIX_MODE, &old_mode);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glViewport(0, 0, w, h); glDisable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
    glDisable(GL_TEXTURE_2D);
    float y0 = h * 0.42f, y1 = y0 + 90;
    glColor4f(0, 0, 0, 0.85f);
    glBegin(GL_QUADS); glVertex2f(w * 0.2f, y0); glVertex2f(w * 0.8f, y0); glVertex2f(w * 0.8f, y1); glVertex2f(w * 0.2f, y1); glEnd();
    GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glEnable(GL_TEXTURE_2D); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); glColor4f(1, 1, 1, 1);
    float x = w * 0.5f - tw / 2.f, y = y0 + (90 - th) / 2.f;
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x, y); glTexCoord2f(1, 0); glVertex2f(x + tw, y);
    glTexCoord2f(1, 1); glVertex2f(x + tw, y + th); glTexCoord2f(0, 1); glVertex2f(x, y + th);
    glEnd();
    glDeleteTextures(1, &tex);
    glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    glBindTexture(GL_TEXTURE_2D, old_tex); glMatrixMode(old_mode);
}

// ======================================================================= game lifecycle
namespace {
uint32_t o_view, o_renderer, o_kb, o_sound, o_text, o_fm, o_df;
int GX, GY, GW = 1280, GH = 720;            // game area inside the window (top-left origin), always 16:9
void touch(int phase, float x, float y) {   // x, y: window pixels
    x -= GX; y -= GY;
    uint32_t fx, fy; memcpy(&fx, &x, 4); memcpy(&fy, &y, 4);
    jni_native("GameGLSurfaceView", "nativeDefaultTouchStart", o_view, {(uint32_t)phase});
    jni_native("GameGLSurfaceView", "nativeDefaultTouch", o_view, {fx, fy});
    jni_native("GameGLSurfaceView", "nativeDefaultTouchEnd", o_view, {});
}
}  // namespace

void jni_keyboard_done(const std::string &text) {
    jni_native("KeyboardView", "nativeKeyboardViewSetIsDone", o_kb, {1});
    jni_native("KeyboardView", "nativeKeyboardViewSetLastText", o_kb, {jni_new_string(text)});
}
static std::mutex dl_mu;
static std::vector<int> dl_failed;
static uint32_t o_fdl;
void jni_image_download_failed(int id) { std::lock_guard<std::mutex> l(dl_mu); dl_failed.push_back(id); }
static void deliver_downloads() {
    std::vector<int> v; { std::lock_guard<std::mutex> l(dl_mu); v.swap(dl_failed); }
    for (int id : v) jni_native("FileDownloader", "nativeImageDownloadCallback", o_fdl, {(uint32_t)id, 0});
}
void jni_music_end() { jni_native("Sound", "nativeSoundMusicEndCallBack", o_sound, {}); }

// test automation: PW2_SCRIPT="frame:tap:x:y;frame:shot:file.ppm;frame:text:abc;frame:back;frame:quit" (x, y in 0..1)
namespace {
struct Step { long frame; std::string op; float x, y; std::string arg; };
std::vector<Step> script;
void parse_script() {
    const char *e = getenv("PW2_SCRIPT"); if (!e) return;
    std::string s = e; size_t p = 0;
    while (p < s.size()) {
        size_t q = s.find(';', p); if (q == std::string::npos) q = s.size();
        std::string it = s.substr(p, q - p); p = q + 1;
        std::vector<std::string> f; size_t a = 0;
        while (true) { size_t b = it.find(':', a); f.push_back(it.substr(a, b == std::string::npos ? std::string::npos : b - a)); if (b == std::string::npos) break; a = b + 1; }
        if (f.size() < 2) continue;
        Step st{atol(f[0].c_str()), f[1], 0, 0, ""};
        if (st.op == "tap" && f.size() >= 4) { st.x = (float)atof(f[2].c_str()); st.y = (float)atof(f[3].c_str()); }
        if (f.size() >= 3) st.arg = f[2];
        script.push_back(st);
    }
}
void save_ppm(const std::string &path, int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE *f = host::fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) fwrite(&px[(size_t)y * w * 3], 1, (size_t)w * 3, f);
    fclose(f);
}
}  // namespace

// LoL-style battle hotkeys. Button centres as fractions of the game area (always 16:9, see GX/GW).
namespace {
const uint32_t SCENE_MANAGER = 0x400fa0, SCENE_BATTLE = 0xf;   // SceneManager object in libGame .bss; +0x24 = scene type
struct Hotkey { SDL_Keycode key; bool ctrl; float x, y; const char *label; float bx, by; };   // bx,by: badge offset
const Hotkey HOTKEYS[] = {
    // skills (Ctrl = level up with the "+" above the skill, like LoL)
    {SDLK_q, false, 0.625f, 0.878f, "Q", 0.030f, -0.060f}, {SDLK_w, false, 0.741f, 0.878f, "W", 0.030f, -0.060f},
    {SDLK_e, false, 0.845f, 0.878f, "E", 0.030f, -0.060f}, {SDLK_r, false, 0.948f, 0.878f, "R", 0.030f, -0.060f},
    {SDLK_q, true, 0.625f, 0.750f, nullptr, 0, 0}, {SDLK_w, true, 0.741f, 0.750f, nullptr, 0, 0},
    {SDLK_e, true, 0.845f, 0.750f, nullptr, 0, 0}, {SDLK_r, true, 0.948f, 0.750f, nullptr, 0, 0},
    // potions / items
    {SDLK_1, false, 0.047f, 0.917f, "1", 0.024f, -0.048f}, {SDLK_2, false, 0.125f, 0.917f, "2", 0.024f, -0.048f},
    {SDLK_3, false, 0.205f, 0.917f, "3", 0.024f, -0.048f}, {SDLK_4, false, 0.283f, 0.917f, "4", 0.024f, -0.048f},
    // left column: summon minion, target, camera
    {SDLK_d, false, 0.053f, 0.328f, "D", 0.030f, -0.042f}, {SDLK_f, false, 0.053f, 0.486f, "F", 0.030f, -0.042f},
    {SDLK_SPACE, false, 0.053f, 0.617f, "Space", 0.036f, -0.042f},
    // top right: game speed, pause
    {SDLK_x, false, 0.883f, 0.061f, "X", -0.030f, 0.045f}, {SDLK_p, false, 0.955f, 0.061f, "P", 0.012f, 0.050f},
};
uint32_t scene_type() { return R32(BASE + SCENE_MANAGER + 0x24); }
bool hotkey_pos(SDL_Keycode k, bool ctrl, int W, int H, float &px, float &py) {
    (void)W; (void)H;
    for (auto &h : HOTKEYS) {
        if (h.key != k || h.ctrl != ctrl) continue;
        px = GX + h.x * GW; py = GY + h.y * GH;           // fractions of the (fixed 16:9) game area
        return true;
    }
    return false;
}

// small key badges over the battle HUD, in the game's own colours (dark wood + gold rim)
struct Badge { GLuint tex = 0; int w = 0, h = 0; };
std::map<std::string, Badge> badges;
void draw_badges(int W, int H) {
    if (scene_type() != SCENE_BATTLE) return;
    GLint old_tex = 0, old_mode = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_tex); glGetIntegerv(GL_MATRIX_MODE, &old_mode);
    glPushAttrib(GL_ALL_ATTRIB_BITS); glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glViewport(GX, H - GY - GH, GW, GH); glDisable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, GW, GH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE); glDisable(GL_FOG); glDisable(GL_ALPHA_TEST);
    glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisableClientState(GL_COLOR_ARRAY);
    glEnable(GL_BLEND);
    const float r = GH * 0.024f;                            // badge radius (~17 px at 720p)
    for (auto &h : HOTKEYS) {
        if (!h.label) continue;
        Badge &b = badges[h.label];
        if (!b.tex) {
            std::vector<uint8_t> px = plat::text_string(h.label, 26, 0, 1, 0, -1, 1, b.w, b.h);
            glGenTextures(1, &b.tex); glBindTexture(GL_TEXTURE_2D, b.tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, b.w, b.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        }
        float cx = (h.x + h.bx) * GW, cy = (h.y + h.by) * GH;
        float th = r * 1.25f, tw = th * b.w / (float)b.h;  // text box
        float half = std::max(0.f, tw / 2 - r * 0.55f);    // pill for long labels ("Space")
        glDisable(GL_TEXTURE_2D); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (int pass = 0; pass < 2; pass++) {              // gold rim, then dark wood fill
            float rr = pass ? r * 0.80f : r;
            if (pass) glColor4f(0.23f, 0.13f, 0.06f, 0.92f); else glColor4f(0.96f, 0.76f, 0.27f, 0.95f);
            glBegin(GL_TRIANGLE_FAN); glVertex2f(cx, cy);
            for (int k = 0; k <= 32; k++) {
                float a = k * 6.2831853f / 32, dx = cosf(a) * rr, dy = sinf(a) * rr;
                glVertex2f(cx + dx + (dx >= 0 ? half : -half), cy + dy);
            }
            glEnd();
            if (half > 0) { glBegin(GL_QUADS); glVertex2f(cx - half, cy - rr); glVertex2f(cx + half, cy - rr); glVertex2f(cx + half, cy + rr); glVertex2f(cx - half, cy + rr); glEnd(); }
        }
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, b.tex);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); glColor4f(1.0f, 0.95f, 0.80f, 1.0f);
        float x0 = cx - tw / 2, y0 = cy - th / 2;
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(x0, y0); glTexCoord2f(1, 0); glVertex2f(x0 + tw, y0);
        glTexCoord2f(1, 1); glVertex2f(x0 + tw, y0 + th); glTexCoord2f(0, 1); glVertex2f(x0, y0 + th);
        glEnd();
    }
    glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
    glPopClientAttrib(); glPopAttrib();
    glBindTexture(GL_TEXTURE_2D, old_tex); glMatrixMode(old_mode);
}
}  // namespace

static std::string find_apk(const std::string &dir, int argc, char **argv) {
    if (argc > 1) return argv[1];
    std::vector<uint8_t> probe;                                   // single-file build: the executable is the APK
    if (plat::apk_open(host::exe_path()) && plat::apk_read("AndroidManifest.xml", probe)) return host::exe_path();
    for (const char *rel : {"/PlantsWar2.apk", "/../Resources/PlantsWar2.apk", "/data/PlantsWar2.apk"})
        if (host::exists(dir + rel)) return dir + rel;
    return dir + "/PlantsWar2.apk";
}

int main(int argc, char **argv) {
    std::string dir = host::exe_dir();
    data_dir = getenv("PW2_DATA") ? getenv("PW2_DATA") : host::default_data_dir();
    host::mkdirs(data_dir);
#ifdef _WIN32
    if (!getenv("PW2_CONSOLE")) {                      // GUI subsystem: keep a log next to the save
        host::redirect_stderr(data_dir + "/log.txt");
    }
#endif
    mem_init();
    std::string err;
    apk_path = find_apk(dir, argc, argv);
    if (!plat::apk_open(apk_path)) fatal("Game data not found: %s\nPut PlantsWar2.apk next to the game.", apk_path.c_str());
#ifdef _WIN32
    { std::string rtdir = data_dir + "/runtime"; host::mkdirs(rtdir);                 // single-file exe: unpack SDL2.dll
      if (plat::pack_extract("SDL2.dll", rtdir + "/SDL2.dll")) dyn::sdl_hint_dir = rtdir; }
#endif
    if (!dyn::load_sdl(err)) fatal("%s", err.c_str());
    server_init(data_dir + "/save.json");

    std::vector<uint8_t> lib;
    if (!plat::apk_read("lib/armeabi-v7a/libGame.so", lib)) fatal("libGame.so not in %s", apk_path.c_str());
    std::string libp = data_dir + "/libGame.so";
    { FILE *f = host::fopen(libp, "wb"); if (!f) fatal("cannot write %s", libp.c_str()); fwrite(lib.data(), 1, lib.size(), f); fclose(f); }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) fatal("SDL: %s", SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16); SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    int W = 1280, H = 720;
    if (const char *ws = getenv("PW2_WINDOW")) sscanf(ws, "%dx%d", &W, &H);
    SDL_Window *win = SDL_CreateWindow("Plants War 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) fatal("window: %s", SDL_GetError());
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    if (!gl) fatal("OpenGL 2.1 context: %s", SDL_GetError());
    if (!dyn::load_gl(err)) fatal("%s", err.c_str());
    logf("GL: %s / %s", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    SDL_GL_SetSwapInterval(1);
    glEnable(GL_POINT_SMOOTH); glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST); glShadeModel(GL_SMOOTH);   // onSurfaceCreated

    plat::audio_init();
    plat::text_init();
    jni_init();
    load_library(libp.c_str());
    run_initializers();

    if (getenv("PW2_SELFTEST")) {
        uint32_t js = gstrdup("{\"Code\": \"0\", \"Url\": \"http://127.0.0.1:12380\"}");
        uint32_t v = call(sym_addr("_ZN4JSON5ParseEPKc"), {js});
        logf("selftest JSON::Parse -> 0x%x", v);
        uint32_t n = gstrdup("123"); W32(gstatic(4), 0);
        uint32_t pp = gstatic(4); W32(pp, n);
        logf("selftest ParseInt -> %u", call(sym_addr("_ZN4JSON8ParseIntEPPKc"), {pp}));
    }
    // same order as GameActivity.onCreate on Android
    jni_native("FacebookManager", "nativeInit", jni_instance("com/ideabox/Library/FacebookManager"), {});        // initSocial
    o_sound = jni_instance("com/ideabox/Library/Sound");
    jni_native("Sound", "nativeSoundCallBack", o_sound, {});
    jni_native("InAppPurchaseManager", "nativePurchaseInit", jni_instance("com/ideabox/Library/InAppPurchaseManager"), {2});
    o_view = jni_instance("com/ideabox/Library/GameGLSurfaceView");
    jni_native("GameGLSurfaceView", "nativeDefaultInit", o_view, {jni_new_string(apk_path), jni_new_string(data_dir)});
    jni_native("GameGLSurfaceView", "nativeSetCountryCode", o_view, {2});
    jni_native("GameGLSurfaceView", "nativeSetLanguageCode", o_view, {2});
    jni_native("GameGLSurfaceView", "nativeSetVersion", o_view, {jni_new_string("1.0.6")});
    o_fm = jni_instance("com/ideabox/Library/FileManager");
    jni_native("FileManager", "nativeFileManagerInit", o_fm, {});
    o_fdl = jni_instance("com/ideabox/Library/FileDownloader");                                             // initDefault
    jni_native("FileDownloader", "nativeInit", o_fdl, {});
    o_kb = jni_instance("com/ideabox/Library/KeyboardView");
    jni_native("KeyboardView", "nativeKeyboardViewInit", o_kb, {});
    jni_native("WebDialog", "nativeInit", jni_instance("com/ideabox/Library/WebDialog"), {});
    jni_native("PolicyWebDialog", "nativeInit", jni_instance("com/ideabox/Library/PolicyWebDialog"), {});
    jni_native("GameNotificationManager", "nativeNotificationManagerInit", jni_instance("com/ideabox/Library/GameNotificationManager"), {});  // initGoogle
    jni_native("GoogleApiHelper", "nativeInitialize", jni_instance("com/ideabox/Library/GoogleApiHelper"), {});
    o_text = jni_instance("com/ideabox/Library/TextToImage");
    jni_native("TextToImage", "nativeTextToImageCallBack", o_text, {});
    o_df = jni_instance("com/ideabox/Library/DefaultFunction");
    jni_native("DefaultFunction", "nativeDefaultFunctionInit", o_df, {});
    jni_native("DefaultFunction", "nativeDeviceInfoInit", o_df, {});
    o_renderer = jni_instance("com/ideabox/Library/GameRenderer");
    jni_native("GameGLSurfaceView", "nativeDefaultResume", o_view, {});      // GameActivity.onResume

    SDL_GL_GetDrawableSize(win, &W, &H);
    bool resized = true, running = true, down = false;
    parse_script();
    long frame = 0; int64_t next_frame = 0; bool show_badges = !getenv("PW2_NO_BADGES");
    while (running) {
        frame++;
        if (const char *ctl = getenv("PW2_CTL")) {         // interactive test channel: "tap x y" / "shot file" / "text s" / "back" / "quit"
            if (FILE *cf = host::fopen(ctl, "rb")) {
                char line[512];
                while (fgets(line, sizeof line, cf)) {
                    char op[32] = {0}, arg[480] = {0}; float x = 0, y = 0;
                    if (sscanf(line, "%31s", op) != 1) continue;
                    std::string o = op;
                    if (o == "tap" && sscanf(line, "%*s %f %f", &x, &y) == 2) script.push_back(Step{frame, "tap", x, y, ""});
                    else if (sscanf(line, "%*s %479[^\n]", arg) >= 0) script.push_back(Step{frame, o, 0, 0, arg});
                }
                fclose(cf); host::remove(ctl);
            }
        }
        for (size_t si = 0; si < script.size(); si++) {
            Step st = script[si];
            if (st.frame != frame) continue;
            if (st.op == "tap") { touch(0, GX + st.x * GW, GY + st.y * GH); script.push_back(Step{frame + 4, "up", st.x, st.y, ""}); }
            else if (st.op == "up") touch(2, GX + st.x * GW, GY + st.y * GH);
            else if (st.op == "uppx") touch(2, st.x, st.y);
            else if (st.op == "key") { SDL_Event ke{}; ke.type = SDL_KEYDOWN; ke.key.keysym.sym = SDL_GetKeyFromName(st.arg.c_str()); SDL_PushEvent(&ke); }
            else if (st.op == "shot") save_ppm(st.arg, W, H);
            else if (st.op == "back") jni_native("GameGLSurfaceView", "nativeDefaultBack", o_view, {});
            else if (st.op == "text") { kb_active = false; jni_keyboard_done(st.arg); }
            else if (st.op == "quit") running = false;
            else if (st.op == "peek") {                    // "peek <hex guest addr> <words>"
                unsigned a = 0, n = 8; sscanf(st.arg.c_str(), "%x %u", &a, &n);
                std::string out; char b[32];
                for (unsigned k = 0; k < n; k++) { uint32_t v = R32(a + 4 * k); float f; memcpy(&f, &v, 4); snprintf(b, sizeof b, " %08x(%g)", v, f); out += b; }
                logf("peek %08x:%s", a, out.c_str());
            }
        }
        SDL_Event e; float hx = 0, hy = 0;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: running = false; break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) { SDL_GL_GetDrawableSize(win, &W, &H); resized = true; }
                break;
            case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEMOTION: {
                if (kb_active) break;
                int ww, wh; SDL_GetWindowSize(win, &ww, &wh);
                float sx = (float)W / ww, sy = (float)H / wh;
                bool btn = e.type != SDL_MOUSEMOTION && (e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT);
                if (e.type == SDL_MOUSEBUTTONDOWN && btn) { down = true; touch(0, e.button.x * sx, e.button.y * sy); }   // right click = move too (LoL)
                else if (e.type == SDL_MOUSEBUTTONUP && btn && down) { down = false; touch(2, e.button.x * sx, e.button.y * sy); }
                else if (e.type == SDL_MOUSEMOTION && down) touch(1, e.motion.x * sx, e.motion.y * sy);
                break;
            }
            case SDL_TEXTINPUT: if (kb_active && (int)kb_text.size() < 64) kb_text += e.text.text; break;
            case SDL_KEYDOWN:
                if (kb_active) {
                    if (e.key.keysym.sym == SDLK_BACKSPACE && !kb_text.empty()) {
                        do kb_text.pop_back(); while (!kb_text.empty() && (kb_text.back() & 0xC0) == 0x80);
                    } else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
                        kb_active = false; SDL_StopTextInput(); jni_keyboard_done(kb_text);
                    } else if (e.key.keysym.sym == SDLK_ESCAPE) { kb_active = false; SDL_StopTextInput(); }
                } else if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_AC_BACK) {
                    jni_native("GameGLSurfaceView", "nativeDefaultBack", o_view, {});
                } else if (!e.key.repeat && scene_type() == SCENE_BATTLE &&
                           hotkey_pos(e.key.keysym.sym, (e.key.keysym.mod & KMOD_CTRL) != 0, W, H, hx, hy)) {
                    touch(0, hx, hy); script.push_back(Step{frame + 3, "uppx", hx, hy, ""});
                } else if (e.key.keysym.sym == SDLK_F1) {
                    show_badges = !show_badges;                 // F1: show/hide the key badges
                } else if (e.key.keysym.sym == SDLK_F11) {
                    bool fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                break;
            }
        }
        if (music_ended.exchange(false)) jni_music_end();
        deliver_downloads();
        if (resized) {                                   // letter/pillar-box to 16:9 like the phone: stable HUD layout
            if ((long)W * 9 > (long)H * 16) { GH = H; GW = H * 16 / 9; } else { GW = W; GH = W * 9 / 16; }
            GX = (W - GW) / 2; GY = (H - GH) / 2;
            extern int g_view_x, g_view_y; g_view_x = GX; g_view_y = H - GY - GH;
            jni_native("GameRenderer", "nativeDefaultResize", o_renderer, {(uint32_t)GW, (uint32_t)GH}); resized = false;
        }
        else jni_native("GameRenderer", "nativeDefaultStep", o_renderer, {});
        if (GX || GY) {                                   // black bars around the game area
            glPushAttrib(GL_COLOR_BUFFER_BIT | GL_SCISSOR_BIT);
            glEnable(GL_SCISSOR_TEST); glClearColor(0, 0, 0, 1);
            if (GX) { glScissor(0, 0, GX, H); glClear(GL_COLOR_BUFFER_BIT); glScissor(GX + GW, 0, W - GX - GW, H); glClear(GL_COLOR_BUFFER_BIT); }
            if (GY) { glScissor(0, 0, W, H - GY - GH); glClear(GL_COLOR_BUFFER_BIT); glScissor(0, H - GY, W, GY); glClear(GL_COLOR_BUFFER_BIT); }
            glPopAttrib();
        }
        if (show_badges) draw_badges(W, H);
        draw_keyboard_overlay(W, H);
        SDL_GL_SwapWindow(win);
        // 60 fps cap: the game was written for phones and breaks on zero-length frames
        // (e.g. ScriptManager::Update loops forever when the frame delta is 0 ms)
        const int64_t frame_ns = 16666667;
        int64_t now = host::mono_ns();
        if (next_frame == 0 || now - next_frame > 100000000) next_frame = now;
        next_frame += frame_ns;
        int64_t wait = next_frame - host::mono_ns();
        if (wait > 2000000) host::sleep_ms((int)(wait / 1000000) - 1);
        while (host::mono_ns() < next_frame) {}
    }
    jni_native("GameGLSurfaceView", "nativeDefaultPause", o_view, {});
    SDL_Quit();
    fflush(nullptr);
    std::_Exit(0);                                     // guest threads are still running: skip static destructors
}

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int n = 0; wchar_t **wa = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::string> args; std::vector<char *> argv;
    for (int k = 0; k < n; k++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wa[k], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len - 1 : 0, '\0'); WideCharToMultiByte(CP_UTF8, 0, wa[k], -1, &s[0], len, nullptr, nullptr);
        args.push_back(s);
    }
    for (auto &s : args) argv.push_back(&s[0]);
    argv.push_back(nullptr);
    return main(n, argv.data());
}
#endif
