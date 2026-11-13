# Generates pass-through wrappers for GLES 1.x functions used by libGame.so.
PASS = {
 "glBindTexture": "void|unsigned t, unsigned tex",
 "glBlendFunc": "void|unsigned s, unsigned d",
 "glClear": "void|unsigned m",
 "glClearColor": "void|float r, float g, float b, float a",
 "glClearColorx": "void|int r, int g, int b, int a",
 "glColor4x": "void|int r, int g, int b, int a",
 "glColorPointer": "void|int s, unsigned t, int st, const void* p",
 "glDeleteTextures": "void|int n, const unsigned* t",
 "glDepthFunc": "void|unsigned f",
 "glDepthMask": "void|unsigned char f",
 "glDisable": "void|unsigned c",
 "glDisableClientState": "void|unsigned c",
 "glDrawArrays": "void|unsigned m, int f, int c",
 "glDrawElements": "void|unsigned m, int c, unsigned t, const void* i",
 "glEnable": "void|unsigned c",
 "glEnableClientState": "void|unsigned c",
 "glFogf": "void|unsigned p, float v",
 "glFogfv": "void|unsigned p, const float* v",
 "glFrontFace": "void|unsigned m",
 "glGenTextures": "void|int n, unsigned* t",
 "glGetError": "unsigned|void",
 "glHint": "void|unsigned t, unsigned m",
 "glLightfv": "void|unsigned l, unsigned p, const float* v",
 "glLightxv": "void|unsigned l, unsigned p, const int* v",
 "glLineWidth": "void|float w",
 "glMaterialx": "void|unsigned f, unsigned p, int v",
 "glMaterialxv": "void|unsigned f, unsigned p, const int* v",
 "glNormalPointer": "void|unsigned t, int s, const void* p",
 "glScissor": "void|int x, int y, int w, int h",
 "glShadeModel": "void|unsigned m",
 "glStencilFunc": "void|unsigned f, int r, unsigned m",
 "glStencilOp": "void|unsigned a, unsigned b, unsigned c",
 "glTexCoordPointer": "void|int s, unsigned t, int st, const void* p",
 "glTexEnvf": "void|unsigned t, unsigned p, float v",
 "glTexImage2D": "void|unsigned t, int l, int i, int w, int h, int b, unsigned f, unsigned ty, const void* p",
 "glTexParameteri": "void|unsigned t, unsigned p, int v",
 "glTexParameterx": "void|unsigned t, unsigned p, int v",
 "glVertexPointer": "void|int s, unsigned t, int st, const void* p",
}
TRACK = {  # also forwarded, but after updating the shadow state (implemented in shim.c)
 "glMatrixMode": "void|unsigned m", "glLoadIdentity": "void|void", "glPushMatrix": "void|void",
 "glPopMatrix": "void|void", "glMultMatrixf": "void|const float* m", "glMultMatrixx": "void|const int* m",
 "glLoadMatrixf": "void|const float* m", "glLoadMatrixx": "void|const int* m",
 "glTranslatef": "void|float x, float y, float z", "glTranslatex": "void|int x, int y, int z",
 "glScalef": "void|float x, float y, float z", "glScalex": "void|int x, int y, int z",
 "glRotatef": "void|float a, float x, float y, float z", "glRotatex": "void|int a, int x, int y, int z",
 "glFrustumf": "void|float l, float r, float b, float t, float n, float f",
 "glFrustumx": "void|int l, int r, int b, int t, int n, int f",
 "glOrthof": "void|float l, float r, float b, float t, float n, float f",
 "glOrthox": "void|int l, int r, int b, int t, int n, int f",
 "glViewport": "void|int x, int y, int w, int h",
 "glGetFloatv": "void|unsigned p, float* v", "glGetIntegerv": "void|unsigned p, int* v",
}
def names(args):
    return "" if args == "void" else ", ".join(a.split()[-1].lstrip("*") for a in args.split(","))
out = ["/* generated */", "#include \"shim.h\""]
for fn, sig in {**PASS, **TRACK}.items():
    ret, args = sig.split("|")
    out.append(f"typedef {ret} (*t_{fn})({args});\nstatic t_{fn} r_{fn};")
for fn, sig in PASS.items():
    ret, args = sig.split("|")
    out.append(f"EXPORT {ret} {fn}({args}) {{ {'return ' if ret!='void' else ''}r_{fn}({names(args)}); }}")
out.append("static void resolve_all(void* h) {")
for fn in {**PASS, **TRACK}:
    out.append(f"  r_{fn} = (t_{fn})dlsym(h, \"{fn}\");")
out.append("}")
open("gen.inc", "w").write("\n".join(out) + "\n")
