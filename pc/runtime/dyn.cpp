#define PW2_DYN_IMPL
#include <string>
#include <vector>
#include "dyn.h"
#include "host.h"

#define PW2_DEF(n) decltype(&::n) p_##n;
PW2_SDL_FUNCS(PW2_DEF)
PW2_GL_FUNCS(PW2_DEF)

namespace dyn {
std::string sdl_hint_dir;
bool load_sdl(std::string &err) {
    std::string dir = host::exe_dir();
    std::vector<std::string> cands = {
#if defined(_WIN32)
        sdl_hint_dir + "/SDL2.dll",
#endif
#if defined(_WIN32)
        dir + "/SDL2.dll", "SDL2.dll",
#elif defined(__APPLE__)
        dir + "/../Frameworks/libSDL2-2.0.0.dylib", dir + "/../Frameworks/SDL2.framework/SDL2", dir + "/libSDL2-2.0.0.dylib", dir + "/SDL2.framework/SDL2",
        "/Library/Frameworks/SDL2.framework/SDL2", "/opt/homebrew/lib/libSDL2-2.0.0.dylib", "/usr/local/lib/libSDL2-2.0.0.dylib",
        "libSDL2-2.0.0.dylib",
#else
        dir + "/lib/libSDL2-2.0.so.0", dir + "/libSDL2-2.0.so.0", "libSDL2-2.0.so.0", "libSDL2.so",
#endif
    };
    void *lib = nullptr;
    for (auto &c : cands) if ((lib = host::dl_open(c.c_str()))) break;
    if (!lib) { err = "SDL2 library not found (looked next to the game and in the system)"; return false; }
#define PW2_LOAD(n) p_##n = (decltype(p_##n))host::dl_sym(lib, #n); if (!p_##n) { err = "SDL2 is missing " #n; return false; }
    PW2_SDL_FUNCS(PW2_LOAD)
#undef PW2_LOAD
    return true;
}
bool load_gl(std::string &err) {
#define PW2_LOAD(n) p_##n = (decltype(p_##n))p_SDL_GL_GetProcAddress(#n); if (!p_##n) { err = "OpenGL is missing " #n; return false; }
    PW2_GL_FUNCS(PW2_LOAD)
#undef PW2_LOAD
    return true;
}
}  // namespace dyn
