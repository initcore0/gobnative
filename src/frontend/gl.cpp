// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/gl.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace gob::gl {

namespace {

// SDL_GL_GetProcAddress on Windows asks wglGetProcAddress first, which
// some drivers answer with 1, 2, 3 or -1 instead of nothing for the
// OpenGL 1.1 functions; those come from opengl32.dll itself.
void *resolve(void *(*getProcAddress)(const char *name), const char *name) {
	void *p = getProcAddress(name);
#ifdef _WIN32
	const intptr_t v = reinterpret_cast<intptr_t>(p);
	if (v >= -1 && v <= 3)
		if (HMODULE gl = GetModuleHandleA("opengl32.dll"))
			p = reinterpret_cast<void *>(GetProcAddress(gl, name));
#endif
	return p;
}

} // namespace

#define GOB_GL_DEFINE(ret, name, args) ret(GOB_GLAPI *name) args = nullptr;
GOB_GL_FUNCTIONS(GOB_GL_DEFINE)
#undef GOB_GL_DEFINE

const char *load(void *(*getProcAddress)(const char *name)) {
#define GOB_GL_LOAD(ret, name, args) \
	name = reinterpret_cast<ret(GOB_GLAPI *) args>(resolve(getProcAddress, "gl" #name)); \
	if (!name) \
		return "gl" #name;
	GOB_GL_FUNCTIONS(GOB_GL_LOAD)
#undef GOB_GL_LOAD
	return nullptr;
}

} // namespace gob::gl
