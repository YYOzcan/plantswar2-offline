/*
 * libpw2gl.so - GLES 1.x interposer for Plants War 2.
 *
 * The game reads back GL_MODELVIEW_MATRIX / GL_PROJECTION_MATRIX / GL_VIEWPORT with
 * glGetFloatv/glGetIntegerv to un-project screen corners (CameraManager::ResetClip, picking).
 * Emulated GLES1 stacks (Android emulator gfxstream, ANGLE, ...) do not return usable values
 * there, which collapses the camera clip rect: the map and most objects are culled.
 *
 * This library forwards every GL call to the real libGLESv1_CM.so but keeps its own copy of
 * the matrix stacks and viewport and answers those queries itself.
 */
#include "shim.h"

#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE 0x1702
#define GL_VIEWPORT 0x0BA2
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7

#include "gen.inc"

static void* real_handle;
#define DEPTH 32
typedef struct { float m[DEPTH][16]; int top; } stack_t2;
static stack_t2 st_mv, st_pr, st_tx;
static stack_t2* cur = &st_mv;
static int vp[4];

static float* top(void) { return cur->m[cur->top]; }
static void ident(float* m) { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.f : 0.f; }
static void cpy(float* d, const float* s) { for (int i = 0; i < 16; i++) d[i] = s[i]; }
/* column-major: r = a * b */
static void mul(float* a, const float* b) {
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
            r[c * 4 + rr] = s;
        }
    cpy(a, r);
}
static float fx(int v) { return (float)v / 65536.f; }

static float my_sin(float x) {           /* range-reduced Taylor series, good to ~1e-6 */
    const float PI = 3.14159265358979f, TWO_PI = 6.28318530717959f;
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    if (x > PI / 2) x = PI - x;
    else if (x < -PI / 2) x = -PI - x;
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}
static float my_cos(float x) { return my_sin(x + 1.57079632679490f); }
static float my_sqrt(float v) {
    if (v <= 0) return 0;
    float g = v > 1 ? v : 1;
    for (int i = 0; i < 30; i++) g = 0.5f * (g + v / g);
    return g;
}

static void init(void) {
    ident(st_mv.m[0]); ident(st_pr.m[0]); ident(st_tx.m[0]);
    real_handle = dlopen("libGLESv1_CM.so", RTLD_NOW);
    if (real_handle) resolve_all(real_handle);
}
__attribute__((constructor)) static void ctor(void) { init(); }

EXPORT void glMatrixMode(unsigned m) {
    cur = (m == GL_PROJECTION) ? &st_pr : (m == GL_TEXTURE) ? &st_tx : &st_mv;
    r_glMatrixMode(m);
}
EXPORT void glLoadIdentity(void) { ident(top()); r_glLoadIdentity(); }
EXPORT void glPushMatrix(void) {
    if (cur->top < DEPTH - 1) { cpy(cur->m[cur->top + 1], top()); cur->top++; }
    r_glPushMatrix();
}
EXPORT void glPopMatrix(void) { if (cur->top > 0) cur->top--; r_glPopMatrix(); }
EXPORT void glMultMatrixf(const float* m) { mul(top(), m); r_glMultMatrixf(m); }
EXPORT void glMultMatrixx(const int* m) {
    float f[16]; for (int i = 0; i < 16; i++) f[i] = fx(m[i]);
    mul(top(), f); r_glMultMatrixx(m);
}
EXPORT void glLoadMatrixf(const float* m) { cpy(top(), m); if (r_glLoadMatrixf) r_glLoadMatrixf(m); }
EXPORT void glLoadMatrixx(const int* m) {
    for (int i = 0; i < 16; i++) top()[i] = fx(m[i]);
    if (r_glLoadMatrixx) r_glLoadMatrixx(m);
}
static void translate(float x, float y, float z) {
    float t[16]; ident(t); t[12] = x; t[13] = y; t[14] = z; mul(top(), t);
}
static void scale(float x, float y, float z) {
    float t[16]; ident(t); t[0] = x; t[5] = y; t[10] = z; mul(top(), t);
}
static void rotate(float a, float x, float y, float z) {
    float len = my_sqrt(x * x + y * y + z * z);
    if (len == 0) return;
    x /= len; y /= len; z /= len;
    float r = a * 3.14159265358979f / 180.f, c = my_cos(r), s = my_sin(r), ic = 1 - c;
    float t[16] = { x*x*ic + c,   y*x*ic + z*s, z*x*ic - y*s, 0,
                    x*y*ic - z*s, y*y*ic + c,   z*y*ic + x*s, 0,
                    x*z*ic + y*s, y*z*ic - x*s, z*z*ic + c,   0,
                    0, 0, 0, 1 };
    mul(top(), t);
}
static void frustum(float l, float r, float b, float t, float n, float f) {
    float m[16] = { 2*n/(r-l), 0, 0, 0,   0, 2*n/(t-b), 0, 0,
                    (r+l)/(r-l), (t+b)/(t-b), -(f+n)/(f-n), -1,   0, 0, -2*f*n/(f-n), 0 };
    mul(top(), m);
}
static void ortho(float l, float r, float b, float t, float n, float f) {
    float m[16] = { 2/(r-l), 0, 0, 0,   0, 2/(t-b), 0, 0,   0, 0, -2/(f-n), 0,
                    -(r+l)/(r-l), -(t+b)/(t-b), -(f+n)/(f-n), 1 };
    mul(top(), m);
}
EXPORT void glTranslatef(float x, float y, float z) { translate(x, y, z); r_glTranslatef(x, y, z); }
EXPORT void glTranslatex(int x, int y, int z) { translate(fx(x), fx(y), fx(z)); r_glTranslatex(x, y, z); }
EXPORT void glScalef(float x, float y, float z) { scale(x, y, z); r_glScalef(x, y, z); }
EXPORT void glScalex(int x, int y, int z) { scale(fx(x), fx(y), fx(z)); r_glScalex(x, y, z); }
EXPORT void glRotatef(float a, float x, float y, float z) { rotate(a, x, y, z); if (r_glRotatef) r_glRotatef(a, x, y, z); }
EXPORT void glRotatex(int a, int x, int y, int z) { rotate(fx(a), fx(x), fx(y), fx(z)); r_glRotatex(a, x, y, z); }
EXPORT void glFrustumf(float l, float r, float b, float t, float n, float f) { frustum(l, r, b, t, n, f); r_glFrustumf(l, r, b, t, n, f); }
EXPORT void glFrustumx(int l, int r, int b, int t, int n, int f) {
    frustum(fx(l), fx(r), fx(b), fx(t), fx(n), fx(f)); r_glFrustumx(l, r, b, t, n, f);
}
EXPORT void glOrthof(float l, float r, float b, float t, float n, float f) { ortho(l, r, b, t, n, f); r_glOrthof(l, r, b, t, n, f); }
EXPORT void glOrthox(int l, int r, int b, int t, int n, int f) {
    ortho(fx(l), fx(r), fx(b), fx(t), fx(n), fx(f)); if (r_glOrthox) r_glOrthox(l, r, b, t, n, f);
}
EXPORT void glViewport(int x, int y, int w, int h) { vp[0] = x; vp[1] = y; vp[2] = w; vp[3] = h; r_glViewport(x, y, w, h); }
EXPORT void glGetFloatv(unsigned p, float* v) {
    if (p == GL_MODELVIEW_MATRIX) { cpy(v, st_mv.m[st_mv.top]); return; }
    if (p == GL_PROJECTION_MATRIX) { cpy(v, st_pr.m[st_pr.top]); return; }
    if (p == GL_VIEWPORT && (vp[2] | vp[3])) { for (int i = 0; i < 4; i++) v[i] = (float)vp[i]; return; }
    r_glGetFloatv(p, v);
}
EXPORT void glGetIntegerv(unsigned p, int* v) {
    if (p == GL_VIEWPORT && (vp[2] | vp[3])) { for (int i = 0; i < 4; i++) v[i] = vp[i]; return; }
    r_glGetIntegerv(p, v);
}
