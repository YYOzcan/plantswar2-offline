// Host platform services (SDL window/input/audio, text rasterizer, APK assets).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace plat {
// assets inside the APK zip ("assets/Common/...")
bool apk_open(const std::string &path);
bool pack_extract(const std::string &name, const std::string &dest);
bool apk_read(const std::string &entry, std::vector<uint8_t> &out);

// TextToImage
void text_init();
void text_set_font(int type);                       // 0 NanumGothic, 1 THSarabun
std::vector<uint8_t> text_string(const std::string &utf8, int size, int width, int bold, int wordBreak, int align,
                                 int multiLine, int &w, int &h);
std::vector<uint8_t> text_char(const std::string &utf8, int size, int width, int bold, int &w, int &h);
int text_char_width(const std::string &utf8, int size, int bold);

// Sound
void audio_init();
void music_load(const std::string &path);
void music_play(int loop);
void music_stop();
void music_pause();
void music_resume();
void music_volume(float v);
int effect_load(const std::string &path);
void effect_play(int id, int loop);
void effect_stop(int id);
void effect_pause_all();
void effect_resume_all();
void effect_volume(float v);

// text entry (nickname, coupon...)
void keyboard_show(const std::string &text, int maxlen);
}  // namespace plat

// JNI side used by the platform: deliver keyboard results / music end to the game
void jni_keyboard_done(const std::string &text);
void jni_music_end();
void jni_image_download_failed(int id);   // queued, delivered from the main loop
