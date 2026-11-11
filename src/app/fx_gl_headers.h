// fx_gl_headers.h - the GLES2 declarations, from the system when it has them (Android NDK) and from SDL's built-in copy otherwise
// (a desktop box without the Mesa development headers).  We only use the types and constants: every function is loaded through
// SDL_GL_GetProcAddress, so nothing links against a GL library.
#pragma once
#include <SDL.h>

#if !defined(SDL_USE_BUILTIN_OPENGL_DEFINITIONS) && defined(__has_include)
#if !__has_include(<GLES2/gl2.h>)
#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS 1
#endif
#endif
#include <SDL_opengles2.h>
