// SDL2 and OpenGL are loaded at run time (dlopen / LoadLibrary / SDL_GL_GetProcAddress), so the
// executable has no link-time dependency on either: the same code builds for Linux, Windows and macOS.
#pragma once
#include <string>
#include "../third_party/SDL2/SDL.h"
#include "../third_party/SDL2/SDL_opengl.h"
#ifdef _WIN32            /* winbase.h defines GPTR (GlobalAlloc flags): restore ours */
#undef GPTR
#define GPTR(x) ((void *)GP(x))
#endif

#define PW2_SDL_FUNCS(X) X(SDL_Init) X(SDL_Quit) X(SDL_GetError) X(SDL_CreateWindow) X(SDL_GetWindowFlags) \
    X(SDL_GetWindowSize) X(SDL_SetWindowFullscreen) X(SDL_GL_CreateContext) X(SDL_GL_GetDrawableSize) X(SDL_GL_SetAttribute) \
    X(SDL_GL_SetSwapInterval) X(SDL_GL_SwapWindow) X(SDL_GL_GetProcAddress) X(SDL_OpenAudioDevice) X(SDL_PauseAudioDevice) \
    X(SDL_PollEvent) X(SDL_StartTextInput) X(SDL_StopTextInput) X(SDL_ShowSimpleMessageBox) X(SDL_SetHint) \
    X(SDL_GetVersion) X(SDL_GetKeyFromName) X(SDL_PushEvent)

#define PW2_GL_FUNCS(X) X(glAlphaFunc) X(glBegin) X(glBindTexture) X(glBlendFunc) X(glClear) X(glClearColor) X(glColor4f) \
    X(glColorMask) X(glColorPointer) X(glCullFace) X(glDeleteTextures) X(glDepthFunc) X(glDepthMask) X(glDisable) \
    X(glDisableClientState) X(glDrawArrays) X(glDrawElements) X(glEnable) X(glEnableClientState) X(glEnd) X(glFinish) \
    X(glFlush) X(glFogf) X(glFogfv) X(glFrontFace) X(glFrustum) X(glGenTextures) X(glGetError) X(glGetFloatv) \
    X(glGetIntegerv) X(glHint) X(glLightfv) X(glLineWidth) X(glLoadIdentity) X(glLoadMatrixf) X(glMaterialf) \
    X(glMaterialfv) X(glMatrixMode) X(glMultMatrixf) X(glNormalPointer) X(glOrtho) X(glPixelStorei) X(glPopAttrib) \
    X(glPopMatrix) X(glPushAttrib) X(glPushMatrix) X(glRotatef) X(glScalef) X(glScissor) X(glShadeModel) X(glStencilFunc) \
    X(glStencilOp) X(glTexCoord2f) X(glTexCoordPointer) X(glTexEnvf) X(glTexEnvi) X(glTexImage2D) X(glTexParameterf) \
    X(glTexParameteri) X(glTexSubImage2D) X(glTranslatef) X(glVertex2f) X(glVertexPointer) X(glViewport) X(glGetString) X(glReadPixels) X(glPushClientAttrib) X(glPopClientAttrib)

#define PW2_DECL(n) extern decltype(&::n) p_##n;
PW2_SDL_FUNCS(PW2_DECL)
PW2_GL_FUNCS(PW2_DECL)
#undef PW2_DECL

namespace dyn {
extern std::string sdl_hint_dir;   // searched first
bool load_sdl(std::string &err);   // before any SDL call
bool load_gl(std::string &err);    // after the GL context exists
}

#ifndef PW2_DYN_IMPL
#define PW2_USE(n) p_##n
#define SDL_Init PW2_USE(SDL_Init)
#define SDL_Quit PW2_USE(SDL_Quit)
#define SDL_GetError PW2_USE(SDL_GetError)
#define SDL_CreateWindow PW2_USE(SDL_CreateWindow)
#define SDL_GetWindowFlags PW2_USE(SDL_GetWindowFlags)
#define SDL_GetWindowSize PW2_USE(SDL_GetWindowSize)
#define SDL_SetWindowFullscreen PW2_USE(SDL_SetWindowFullscreen)
#define SDL_GL_CreateContext PW2_USE(SDL_GL_CreateContext)
#define SDL_GL_GetDrawableSize PW2_USE(SDL_GL_GetDrawableSize)
#define SDL_GL_SetAttribute PW2_USE(SDL_GL_SetAttribute)
#define SDL_GL_SetSwapInterval PW2_USE(SDL_GL_SetSwapInterval)
#define SDL_GL_SwapWindow PW2_USE(SDL_GL_SwapWindow)
#define SDL_GL_GetProcAddress PW2_USE(SDL_GL_GetProcAddress)
#define SDL_OpenAudioDevice PW2_USE(SDL_OpenAudioDevice)
#define SDL_PauseAudioDevice PW2_USE(SDL_PauseAudioDevice)
#define SDL_PollEvent PW2_USE(SDL_PollEvent)
#define SDL_StartTextInput PW2_USE(SDL_StartTextInput)
#define SDL_StopTextInput PW2_USE(SDL_StopTextInput)
#define SDL_ShowSimpleMessageBox PW2_USE(SDL_ShowSimpleMessageBox)
#define SDL_SetHint PW2_USE(SDL_SetHint)
#define SDL_GetVersion PW2_USE(SDL_GetVersion)
#define SDL_GetKeyFromName PW2_USE(SDL_GetKeyFromName)
#define SDL_PushEvent PW2_USE(SDL_PushEvent)
#define glAlphaFunc PW2_USE(glAlphaFunc)
#define glBegin PW2_USE(glBegin)
#define glBindTexture PW2_USE(glBindTexture)
#define glBlendFunc PW2_USE(glBlendFunc)
#define glClear PW2_USE(glClear)
#define glClearColor PW2_USE(glClearColor)
#define glColor4f PW2_USE(glColor4f)
#define glColorMask PW2_USE(glColorMask)
#define glColorPointer PW2_USE(glColorPointer)
#define glCullFace PW2_USE(glCullFace)
#define glDeleteTextures PW2_USE(glDeleteTextures)
#define glDepthFunc PW2_USE(glDepthFunc)
#define glDepthMask PW2_USE(glDepthMask)
#define glDisable PW2_USE(glDisable)
#define glDisableClientState PW2_USE(glDisableClientState)
#define glDrawArrays PW2_USE(glDrawArrays)
#define glDrawElements PW2_USE(glDrawElements)
#define glEnable PW2_USE(glEnable)
#define glEnableClientState PW2_USE(glEnableClientState)
#define glEnd PW2_USE(glEnd)
#define glFinish PW2_USE(glFinish)
#define glFlush PW2_USE(glFlush)
#define glFogf PW2_USE(glFogf)
#define glFogfv PW2_USE(glFogfv)
#define glFrontFace PW2_USE(glFrontFace)
#define glFrustum PW2_USE(glFrustum)
#define glGenTextures PW2_USE(glGenTextures)
#define glGetError PW2_USE(glGetError)
#define glGetFloatv PW2_USE(glGetFloatv)
#define glGetIntegerv PW2_USE(glGetIntegerv)
#define glHint PW2_USE(glHint)
#define glLightfv PW2_USE(glLightfv)
#define glLineWidth PW2_USE(glLineWidth)
#define glLoadIdentity PW2_USE(glLoadIdentity)
#define glLoadMatrixf PW2_USE(glLoadMatrixf)
#define glMaterialf PW2_USE(glMaterialf)
#define glMaterialfv PW2_USE(glMaterialfv)
#define glMatrixMode PW2_USE(glMatrixMode)
#define glMultMatrixf PW2_USE(glMultMatrixf)
#define glNormalPointer PW2_USE(glNormalPointer)
#define glOrtho PW2_USE(glOrtho)
#define glPixelStorei PW2_USE(glPixelStorei)
#define glPopAttrib PW2_USE(glPopAttrib)
#define glPopMatrix PW2_USE(glPopMatrix)
#define glPushAttrib PW2_USE(glPushAttrib)
#define glPushMatrix PW2_USE(glPushMatrix)
#define glRotatef PW2_USE(glRotatef)
#define glScalef PW2_USE(glScalef)
#define glScissor PW2_USE(glScissor)
#define glShadeModel PW2_USE(glShadeModel)
#define glStencilFunc PW2_USE(glStencilFunc)
#define glStencilOp PW2_USE(glStencilOp)
#define glTexCoord2f PW2_USE(glTexCoord2f)
#define glTexCoordPointer PW2_USE(glTexCoordPointer)
#define glTexEnvf PW2_USE(glTexEnvf)
#define glTexEnvi PW2_USE(glTexEnvi)
#define glTexImage2D PW2_USE(glTexImage2D)
#define glTexParameterf PW2_USE(glTexParameterf)
#define glTexParameteri PW2_USE(glTexParameteri)
#define glTexSubImage2D PW2_USE(glTexSubImage2D)
#define glTranslatef PW2_USE(glTranslatef)
#define glVertex2f PW2_USE(glVertex2f)
#define glVertexPointer PW2_USE(glVertexPointer)
#define glViewport PW2_USE(glViewport)
#define glGetString PW2_USE(glGetString)
#define glReadPixels PW2_USE(glReadPixels)
#define glPushClientAttrib PW2_USE(glPushClientAttrib)
#define glPopClientAttrib PW2_USE(glPopClientAttrib)
#endif
