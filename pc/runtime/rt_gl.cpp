// OpenGL ES 1.x imports -> desktop OpenGL (compatibility profile, same fixed-function pipeline).
// Guest pointers are host pointers, so vertex arrays / pixel data pass straight through.
#include "rt.h"
#include "dyn.h"

using namespace rt;

#define A(i) arg(c, i)
#define F(i) argf(c, i)
#define X(i) ((float)(int32_t)arg(c, i) / 65536.0f)      // GLfixed 16.16

IMP(glBindTexture) { glBindTexture(A(0), A(1)); }
IMP(glBlendFunc) { glBlendFunc(A(0), A(1)); }
IMP(glClear) { glClear(A(0)); }
IMP(glClearColor) { glClearColor(F(0), F(1), F(2), F(3)); }
IMP(glClearColorx) { glClearColor(X(0), X(1), X(2), X(3)); }
IMP(glColor4x) { glColor4f(X(0), X(1), X(2), X(3)); }
IMP(glColor4f) { glColor4f(F(0), F(1), F(2), F(3)); }
IMP(glColorPointer) { glColorPointer(A(0), A(1), A(2), GPTR(A(3))); }
IMP(glDeleteTextures) { glDeleteTextures(A(0), (const GLuint *)GPTR(A(1))); }
IMP(glDepthFunc) { glDepthFunc(A(0)); }
IMP(glDepthMask) { glDepthMask((GLboolean)A(0)); }
IMP(glDisable) { glDisable(A(0)); }
IMP(glDisableClientState) { glDisableClientState(A(0)); }
IMP(glDrawArrays) { glDrawArrays(A(0), A(1), A(2)); }
IMP(glDrawElements) { glDrawElements(A(0), A(1), A(2), GPTR(A(3))); }
IMP(glEnable) { glEnable(A(0)); }
IMP(glEnableClientState) { glEnableClientState(A(0)); }
IMP(glFogf) { glFogf(A(0), F(1)); }
IMP(glFogfv) { glFogfv(A(0), (const GLfloat *)GPTR(A(1))); }
IMP(glFrontFace) { glFrontFace(A(0)); }
IMP(glFrustumf) { glFrustum(F(0), F(1), F(2), F(3), F(4), F(5)); }
IMP(glFrustumx) { glFrustum(X(0), X(1), X(2), X(3), X(4), X(5)); }
IMP(glOrthof) { glOrtho(F(0), F(1), F(2), F(3), F(4), F(5)); }
IMP(glOrthox) { glOrtho(X(0), X(1), X(2), X(3), X(4), X(5)); }
IMP(glGenTextures) { glGenTextures(A(0), (GLuint *)GPTR(A(1))); }
IMP(glGetError) { c->r[0] = glGetError(); }
IMP(glGetFloatv) { glGetFloatv(A(0), (GLfloat *)GPTR(A(1))); }
IMP(glGetIntegerv) { glGetIntegerv(A(0), (GLint *)GPTR(A(1))); }
IMP(glHint) { glHint(A(0), A(1)); }
IMP(glLightfv) { glLightfv(A(0), A(1), (const GLfloat *)GPTR(A(2))); }
IMP(glLightxv) { const int32_t *p = (const int32_t *)GPTR(A(2)); float f[4]; for (int k = 0; k < 4; k++) f[k] = p[k] / 65536.0f; glLightfv(A(0), A(1), f); }
IMP(glLineWidth) { glLineWidth(F(0)); }
IMP(glLoadIdentity) { glLoadIdentity(); }
IMP(glMaterialx) { glMaterialf(A(0), A(1), X(2)); }
IMP(glMaterialxv) { const int32_t *p = (const int32_t *)GPTR(A(2)); float f[4]; for (int k = 0; k < 4; k++) f[k] = p[k] / 65536.0f; glMaterialfv(A(0), A(1), f); }
IMP(glMatrixMode) { glMatrixMode(A(0)); }
IMP(glMultMatrixf) { glMultMatrixf((const GLfloat *)GPTR(A(0))); }
IMP(glMultMatrixx) { const int32_t *p = (const int32_t *)GPTR(A(0)); float f[16]; for (int k = 0; k < 16; k++) f[k] = p[k] / 65536.0f; glMultMatrixf(f); }
IMP(glLoadMatrixf) { glLoadMatrixf((const GLfloat *)GPTR(A(0))); }
IMP(glNormalPointer) { glNormalPointer(A(0), A(1), GPTR(A(2))); }
IMP(glPopMatrix) { glPopMatrix(); }
IMP(glPushMatrix) { glPushMatrix(); }
IMP(glRotatef) { glRotatef(F(0), F(1), F(2), F(3)); }
IMP(glRotatex) { glRotatef(X(0), X(1), X(2), X(3)); }
IMP(glScalef) { glScalef(F(0), F(1), F(2)); }
IMP(glScalex) { glScalef(X(0), X(1), X(2)); }
IMP(glTranslatef) { glTranslatef(F(0), F(1), F(2)); }
IMP(glTranslatex) { glTranslatef(X(0), X(1), X(2)); }
IMP(glScissor) { glScissor(A(0), A(1), A(2), A(3)); }
IMP(glShadeModel) { glShadeModel(A(0)); }
IMP(glStencilFunc) { glStencilFunc(A(0), A(1), A(2)); }
IMP(glStencilOp) { glStencilOp(A(0), A(1), A(2)); }
IMP(glTexCoordPointer) { glTexCoordPointer(A(0), A(1), A(2), GPTR(A(3))); }
IMP(glTexEnvf) { glTexEnvf(A(0), A(1), F(2)); }
IMP(glTexEnvi) { glTexEnvi(A(0), A(1), A(2)); }
IMP(glTexImage2D) { glTexImage2D(A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7), GPTR(A(8))); }
IMP(glTexSubImage2D) { glTexSubImage2D(A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7), GPTR(A(8))); }
IMP(glTexParameteri) { glTexParameteri(A(0), A(1), A(2)); }
IMP(glTexParameterx) { glTexParameteri(A(0), A(1), A(2)); }     // enum-valued params are passed unscaled
IMP(glTexParameterf) { glTexParameterf(A(0), A(1), F(2)); }
IMP(glVertexPointer) { glVertexPointer(A(0), A(1), A(2), GPTR(A(3))); }
IMP(glViewport) { glViewport(A(0), A(1), A(2), A(3)); }
IMP(glPixelStorei) { glPixelStorei(A(0), A(1)); }
IMP(glAlphaFunc) { glAlphaFunc(A(0), F(1)); }
IMP(glColorMask) { glColorMask(A(0), A(1), A(2), A(3)); }
IMP(glCullFace) { glCullFace(A(0)); }
IMP(glFlush) { glFlush(); }
IMP(glFinish) { glFinish(); }
