// Fake JNI: JNIEnv / JavaVM tables live in guest memory and trap into this file; the Java glue classes of
// the APK (TextToImage, Sound, KeyboardView, DefaultFunction, ...) are reimplemented here on top of plat::.
#include "rt.h"
#include "platform.h"
#include <map>
#include <mutex>
#include <unordered_map>
#include "host.h"

using namespace rt;

namespace {
enum Kind { K_CLASS, K_OBJ, K_STR, K_ARR };
struct Obj { Kind kind; std::string cls; std::string str; std::vector<uint8_t> bytes; };
struct Method { std::string cls, name, sig; bool is_static; };

std::mutex mu;
std::vector<Obj *> objs(1, nullptr);               // handle = 0x100000 + index
std::vector<Method> methods(1);                    // id = 0x200000 + index
std::unordered_map<uint32_t, Obj *> elems_owner;   // guest elements buffer -> array (GetByteArrayElements)

uint32_t new_obj(Kind k, const std::string &cls, const std::string &str = "") {
    std::lock_guard<std::mutex> l(mu);
    objs.push_back(new Obj{k, cls, str, {}});
    return 0x100000u + (uint32_t)(objs.size() - 1);
}
Obj *obj(uint32_t h) {
    std::lock_guard<std::mutex> l(mu);
    uint32_t i = h - 0x100000u;
    return (h >= 0x100000u && i < objs.size()) ? objs[i] : nullptr;
}
std::map<std::string, uint32_t> class_handles;
uint32_t class_handle(const std::string &name) {
    auto it = class_handles.find(name);
    if (it != class_handles.end()) return it->second;
    return class_handles[name] = new_obj(K_CLASS, name);
}
uint32_t instance(const std::string &cls) { return new_obj(K_OBJ, cls); }
std::string short_class(const std::string &c) { size_t p = c.rfind('/'); return p == std::string::npos ? c : c.substr(p + 1); }

const uint32_t JNI_TRAP = 0x0F200000u, VM_TRAP = 0x0F300000u;
uint32_t g_env, g_vm;

// ---- argument decoding for Call*Method{,V,A}
struct Args {
    std::vector<uint64_t> v;
    uint32_t i(int k) const { return (uint32_t)v[k]; }
    float f(int k) const { double d; uint64_t u = v[k]; memcpy(&d, &u, 8); return (float)d; }
    std::string s(int k) const { Obj *o = obj(i(k)); return o ? o->str : std::string(); }
};
std::vector<char> sig_args(const std::string &sig) {
    std::vector<char> r;
    for (size_t p = 1; p < sig.size() && sig[p] != ')'; p++) {
        char ch = sig[p];
        if (ch == 'L') { while (sig[p] != ';') p++; r.push_back('L'); }
        else if (ch == '[') { while (sig[p] == '[') p++; if (sig[p] == 'L') while (sig[p] != ';') p++; r.push_back('L'); }
        else r.push_back(ch);
    }
    return r;
}
Args read_args(const std::string &sig, int mode, VA &va, uint32_t jvalues) {   // mode 0 varargs/va_list, 1 jvalue[]
    Args a;
    int k = 0;
    for (char t : sig_args(sig)) {
        if (mode == 1) {
            uint32_t p = jvalues + 8 * k++;
            if (t == 'J') a.v.push_back((uint64_t)R32(p) | ((uint64_t)R32(p + 4) << 32));
            else if (t == 'D') a.v.push_back((uint64_t)R32(p) | ((uint64_t)R32(p + 4) << 32));
            else if (t == 'F') { double d = RVF(p); uint64_t u; memcpy(&u, &d, 8); a.v.push_back(u); }
            else a.v.push_back(R32(p));
        } else {
            if (t == 'J' || t == 'D' || t == 'F') a.v.push_back(va.next64());   // float promoted to double
            else a.v.push_back(va.next32());
        }
    }
    return a;
}

// ---- results
struct Ret { uint64_t v = 0; };
uint32_t new_string(const std::string &s) { return new_obj(K_STR, "java/lang/String", s); }
uint32_t new_bytes(std::vector<uint8_t> &&b) { uint32_t h = new_obj(K_ARR, "[B"); obj(h)->bytes = std::move(b); return h; }

// ---- Java glue implementations ----------------------------------------------------------------
int text_w, text_h;
Ret java_call(const std::string &cls, const std::string &name, const std::string &sig, const Args &a) {
    std::string c = short_class(cls);
    Ret r;
    if (c == "TextToImage") {
        if (name == "SetFont") plat::text_set_font(a.s(0) == "NanumGothic" ? 0 : 1);
        else if (name == "GetBitmapFromString")
            r.v = new_bytes(plat::text_string(a.s(0), a.i(1), a.i(2), a.i(3), a.i(4), (int32_t)a.i(5), a.i(6), text_w, text_h));
        else if (name == "GetBitmapFromChar") r.v = new_bytes(plat::text_char(a.s(0), a.i(1), a.i(2), a.i(3), text_w, text_h));
        else if (name == "GetBitmapWidth") r.v = text_w;
        else if (name == "GetBitmapHeight") r.v = text_h;
        else if (name == "GetCharWidth") r.v = plat::text_char_width(a.s(0), a.i(1), a.i(2));
        return r;
    }
    if (c == "Sound") {
        if (name == "LoadMusic") plat::music_load(a.s(0));
        else if (name == "PlayMusic") plat::music_play((int32_t)a.i(0));
        else if (name == "StopMusic") plat::music_stop();
        else if (name == "PauseMusic") plat::music_pause();
        else if (name == "ResumeMusic") plat::music_resume();
        else if (name == "SetMusicVolume") plat::music_volume(a.f(0));
        else if (name == "SetEffectVolume") plat::effect_volume(a.f(0));
        else if (name == "LoadEffect") r.v = (uint32_t)plat::effect_load(a.s(0));
        else if (name == "PlayEffect") plat::effect_play(a.i(0), (int32_t)a.i(1));
        else if (name == "StopEffect") plat::effect_stop(a.i(0));
        else if (name == "PauseEffect") plat::effect_pause_all();
        else if (name == "ResumeEffect") plat::effect_resume_all();
        return r;
    }
    if (c == "FileDownloader" && name == "ImageDownloader") {   // no internet: report "download failed" on the next frame
        jni_image_download_failed((int)atoi(a.s(0).c_str())); return r;
    }
    if (c == "KeyboardView" && name == "ShowTextFieldView") { plat::keyboard_show(a.s(0), a.i(1)); return r; }
    if (c == "FileManager") {
        if (name == "MakeDir") host::mkdirs(host_path(a.s(0).c_str()));
        else if (name == "OnCopy") {
            std::vector<uint8_t> b;
            if (plat::apk_read(a.s(0), b) || plat::apk_read("assets/" + a.s(0), b)) {
                FILE *f = host::fopen(host_path(a.s(1).c_str()), "wb");
                if (f) { fwrite(b.data(), 1, b.size(), f); fclose(f); }
            } else logf("FileManager.OnCopy: %s not in apk", a.s(0).c_str());
        }
        return r;
    }
    // everything below: online services / ads / store / social -> offline defaults
    char rt_ = sig.back();
    size_t rp = sig.find(')');
    if (rp != std::string::npos && sig.compare(rp + 1, std::string::npos, "Ljava/lang/String;") == 0) {
        std::string v;
        if (name == "GetXigncodeCookie") v = "offline";
        else if (name == "GetCountryCode") v = "US";
        else if (name == "GetDeviceNetwork") v = "wifi";
        else if (name == "GetIPAddress") v = "127.0.0.1";
        r.v = new_string(v);
    }
    (void)rt_;
    static std::map<std::string, int> seen;
    if (!seen[c + "." + name]++) logf("java %s.%s%s (offline stub)", c.c_str(), name.c_str(), sig.c_str());
    return r;
}

// ---- JNIEnv dispatch -----------------------------------------------------------------------------
void ret_by_sig(CPU *c, const std::string &sig, Ret r) {
    char t = sig[sig.find(')') + 1];
    if (t == 'J' || t == 'D') ret64(c, r.v);
    else c->r[0] = (uint32_t)r.v;
}
Method *meth(uint32_t id) {
    std::lock_guard<std::mutex> l(mu);
    uint32_t i = id - 0x200000u;
    return (id >= 0x200000u && i < methods.size()) ? &methods[i] : nullptr;
}
uint32_t get_method(uint32_t clsh, const char *name, const char *sig, bool st) {
    Obj *o = obj(clsh);
    std::lock_guard<std::mutex> l(mu);
    methods.push_back(Method{o ? o->cls : "?", name, sig, st});
    return 0x200000u + (uint32_t)(methods.size() - 1);
}

void call_method(CPU *c, int variant, bool is_static) {   // variant 0 varargs, 1 va_list, 2 jvalue[]
    uint32_t target = c->r[1];
    Method *m = meth(c->r[2]);
    if (!m) fatal("JNI call with bad method id 0x%x", c->r[2]);
    std::string cls = m->cls;
    if (!is_static) { Obj *o = obj(target); if (o) cls = o->cls; }
    VA va{c, 3, 0, false};
    if (variant == 1) va = VA{c, 0, c->r[3], true};
    Args a = read_args(m->sig, variant == 2 ? 1 : 0, va, c->r[3]);
    ret_by_sig(c, m->sig, java_call(cls, m->name, m->sig, a));
}

void jni_trap(CPU *c, unsigned i) {
    switch (i) {
    case 4: c->r[0] = 0x00010006; return;                                       // GetVersion
    case 6: c->r[0] = class_handle(GSTR(c->r[1])); return;                      // FindClass
    case 15: c->r[0] = 0; return;                                               // ExceptionOccurred
    case 16: case 17: return;                                                   // ExceptionDescribe/Clear
    case 19: c->r[0] = 0; return;                                               // PushLocalFrame
    case 20: c->r[0] = c->r[1]; return;                                         // PopLocalFrame
    case 21: case 25: case 226: c->r[0] = c->r[1]; return;                      // New{Global,Local,WeakGlobal}Ref
    case 22: case 23: case 227: return;                                         // Delete*Ref
    case 24: c->r[0] = c->r[1] == c->r[2]; return;                              // IsSameObject
    case 26: c->r[0] = 0; return;                                               // EnsureLocalCapacity
    case 31: { Obj *o = obj(c->r[1]); c->r[0] = class_handle(o ? o->cls : "?"); return; }   // GetObjectClass
    case 32: c->r[0] = 1; return;                                               // IsInstanceOf
    case 33: c->r[0] = get_method(c->r[1], GSTR(c->r[2]), GSTR(c->r[3]), false); return;
    case 113: c->r[0] = get_method(c->r[1], GSTR(c->r[2]), GSTR(c->r[3]), true); return;
    case 94: case 144: c->r[0] = 0x300000; return;                              // Get{,Static}FieldID
    case 163: c->r[0] = new_string(""); return;                                 // NewString (UTF-16, unused)
    case 164: case 168: { Obj *o = obj(c->r[1]); c->r[0] = o ? (uint32_t)o->str.size() : 0; return; }
    case 167: c->r[0] = c->r[1] ? new_string(GSTR(c->r[1])) : 0; return;        // NewStringUTF
    case 169: { Obj *o = obj(c->r[1]); if (c->r[2]) W8(c->r[2], 0); c->r[0] = gstrdup(o ? o->str.c_str() : ""); return; }
    case 170: gfree(c->r[2]); return;                                           // ReleaseStringUTFChars
    case 171: { Obj *o = obj(c->r[1]); c->r[0] = o ? (uint32_t)o->bytes.size() : 0; return; }   // GetArrayLength
    case 176: { uint32_t h = new_obj(K_ARR, "[B"); obj(h)->bytes.resize(c->r[1]); c->r[0] = h; return; }   // NewByteArray
    case 184: case 222: {                                                       // GetByteArrayElements / Critical
        Obj *o = obj(c->r[1]); if (!o) { c->r[0] = 0; return; }
        uint32_t p = gmalloc(o->bytes.size() + 1); memcpy(GPTR(p), o->bytes.data(), o->bytes.size());
        { std::lock_guard<std::mutex> l(mu); elems_owner[p] = o; }
        if (i == 184 && c->r[2]) W8(c->r[2], 1);
        c->r[0] = p; return;
    }
    case 192: case 223: {                                                       // ReleaseByteArrayElements / Critical
        Obj *o = obj(c->r[1]); uint32_t p = c->r[2]; uint32_t mode = i == 192 ? c->r[3] : 0;
        if (o && p && mode != 2) memcpy(o->bytes.data(), GPTR(p), o->bytes.size());
        if (mode != 1) { std::lock_guard<std::mutex> l(mu); elems_owner.erase(p); gfree(p); }
        return;
    }
    case 200: { Obj *o = obj(c->r[1]); uint32_t st = c->r[2], n = c->r[3], buf = arg(c, 4);   // GetByteArrayRegion
                if (o) memcpy(GPTR(buf), o->bytes.data() + st, n); return; }
    case 208: { Obj *o = obj(c->r[1]); uint32_t st = c->r[2], n = c->r[3], buf = arg(c, 4);   // SetByteArrayRegion
                if (o) memcpy(o->bytes.data() + st, GPTR(buf), n); return; }
    case 217: case 218: c->r[0] = 0; return;                                    // MonitorEnter/Exit
    case 219: W32(c->r[1], g_vm); c->r[0] = 0; return;                          // GetJavaVM
    case 228: c->r[0] = 0; return;                                              // ExceptionCheck
    }
    // Call<Type>Method{,V,A}: 34..63 instance, 114..143 static
    if (i >= 34 && i <= 63) { call_method(c, (i - 34) % 3, false); return; }
    if (i >= 114 && i <= 143) { call_method(c, (i - 114) % 3, true); return; }
    if (i >= 95 && i <= 112) { c->r[0] = 0; c->r[1] = 0; return; }            // fields: default values
    if (i >= 145 && i <= 162) { c->r[0] = 0; c->r[1] = 0; return; }
    fatal("JNI function %u not implemented (from %s)", i, sym_name(c->r[14]));
}

void vm_trap(CPU *c, unsigned i) {
    switch (i) {
    case 4: case 7: case 6: W32(c->r[1], g_env); c->r[0] = 0; return;   // AttachCurrentThread{,AsDaemon}, GetEnv
    case 5: c->r[0] = 0; return;                                       // DetachCurrentThread
    default: c->r[0] = 0; return;
    }
}
}  // namespace

// called from call_addr for the JNI trap ranges
extern "C" bool jni_dispatch(CPU *c, uint32_t addr) {
    if (addr >= JNI_TRAP && addr < JNI_TRAP + 4 * 240) { jni_trap(c, (addr - JNI_TRAP) / 4); return true; }
    if (addr >= VM_TRAP && addr < VM_TRAP + 4 * 16) { vm_trap(c, (addr - VM_TRAP) / 4); return true; }
    return false;
}

namespace rt {
void jni_init() {
    uint32_t tbl = gstatic(4 * 240);
    for (int k = 0; k < 240; k++) W32(tbl + 4 * k, JNI_TRAP + 4 * k);
    g_env = gstatic(8); W32(g_env, tbl);
    uint32_t vtbl = gstatic(4 * 16);
    for (int k = 0; k < 16; k++) W32(vtbl + 4 * k, VM_TRAP + 4 * k);
    g_vm = gstatic(8); W32(g_vm, vtbl);
}
uint32_t jni_env() { return g_env; }
uint32_t jni_new_string(const std::string &s) { return new_string(s); }
uint32_t jni_instance(const char *cls) { return instance(cls); }

// call Java_com_ideabox_Library_<Class>_<method>(env, thiz, args...)
uint32_t jni_native(const char *cls, const char *method, uint32_t thiz, std::initializer_list<uint32_t> args) {
    std::string sym = std::string("Java_com_ideabox_Library_") + cls + "_" + method;
    uint32_t a = sym_addr(sym.c_str());
    if (!a) { logf("native %s missing", sym.c_str()); return 0; }
    std::vector<uint32_t> v{g_env, thiz};
    v.insert(v.end(), args.begin(), args.end());
    switch (v.size()) {
    case 2: return call(a, {v[0], v[1]});
    case 3: return call(a, {v[0], v[1], v[2]});
    case 4: return call(a, {v[0], v[1], v[2], v[3]});
    case 5: return call(a, {v[0], v[1], v[2], v[3], v[4]});
    default: return call(a, {v[0], v[1], v[2], v[3], v[4], v[5]});
    }
}
}  // namespace rt
