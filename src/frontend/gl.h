// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tiny OpenGL 3.2+ core loader: just the entry points and constants the
// presenter uses, resolved at run time through a platform proc-address
// function (SDL_GL_GetProcAddress), so no system GL headers are needed.

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define GOB_GLAPI __stdcall
#else
#define GOB_GLAPI
#endif

namespace gob::gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLchar = char;

constexpr GLenum TEXTURE_2D = 0x0DE1;
constexpr GLenum TEXTURE_MAG_FILTER = 0x2800, TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803;
constexpr GLenum NEAREST = 0x2600, LINEAR = 0x2601, CLAMP_TO_EDGE = 0x812F;
constexpr GLenum RGB = 0x1907, RGBA = 0x1908, RGB8 = 0x8051, RGBA8 = 0x8058, UNSIGNED_BYTE = 0x1401;
constexpr GLenum RED = 0x1903, R8 = 0x8229;
constexpr GLenum SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum UNPACK_ALIGNMENT = 0x0CF5, PACK_ALIGNMENT = 0x0D05, UNPACK_ROW_LENGTH = 0x0CF2;
constexpr GLbitfield COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30;
constexpr GLenum COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82;
constexpr GLenum TEXTURE0 = 0x84C0, TRIANGLE_STRIP = 0x0005, BACK = 0x0405;
constexpr GLenum BLEND = 0x0BE2, DEPTH_TEST = 0x0B71, CULL_FACE = 0x0B44, SCISSOR_TEST = 0x0C11;
constexpr GLenum VERSION = 0x1F02, RENDERER = 0x1F01;
constexpr GLenum FRAMEBUFFER = 0x8D40, COLOR_ATTACHMENT0 = 0x8CE0, FRAMEBUFFER_COMPLETE = 0x8CD5;

#define GOB_GL_FUNCTIONS(X) \
	X(void, GenTextures, (GLsizei n, GLuint *textures)) \
	X(void, DeleteTextures, (GLsizei n, const GLuint *textures)) \
	X(void, BindTexture, (GLenum target, GLuint texture)) \
	X(void, ActiveTexture, (GLenum texture)) \
	X(void, TexParameteri, (GLenum target, GLenum pname, GLint param)) \
	X(void, TexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, \
	                     GLint border, GLenum format, GLenum type, const void *pixels)) \
	X(void, TexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, \
	                        GLsizei height, GLenum format, GLenum type, const void *pixels)) \
	X(void, PixelStorei, (GLenum pname, GLint param)) \
	X(void, ReadBuffer, (GLenum mode)) \
	X(void, ReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)) \
	X(void, Viewport, (GLint x, GLint y, GLsizei width, GLsizei height)) \
	X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
	X(void, Clear, (GLbitfield mask)) \
	X(void, Disable, (GLenum cap)) \
	X(void, Enable, (GLenum cap)) \
	X(void, BlendFunc, (GLenum sfactor, GLenum dfactor)) \
	X(const unsigned char *, GetString, (GLenum name)) \
	X(GLuint, CreateShader, (GLenum type)) \
	X(void, DeleteShader, (GLuint shader)) \
	X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)) \
	X(void, CompileShader, (GLuint shader)) \
	X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint *params)) \
	X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)) \
	X(GLuint, CreateProgram, ()) \
	X(void, DeleteProgram, (GLuint program)) \
	X(void, AttachShader, (GLuint program, GLuint shader)) \
	X(void, LinkProgram, (GLuint program)) \
	X(void, GetProgramiv, (GLuint program, GLenum pname, GLint *params)) \
	X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)) \
	X(void, UseProgram, (GLuint program)) \
	X(GLint, GetUniformLocation, (GLuint program, const GLchar *name)) \
	X(void, Uniform1i, (GLint location, GLint v0)) \
	X(void, Uniform1f, (GLint location, GLfloat v0)) \
	X(void, Uniform2f, (GLint location, GLfloat v0, GLfloat v1)) \
	X(void, GenFramebuffers, (GLsizei n, GLuint *framebuffers)) \
	X(void, DeleteFramebuffers, (GLsizei n, const GLuint *framebuffers)) \
	X(void, BindFramebuffer, (GLenum target, GLuint framebuffer)) \
	X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
	X(GLenum, CheckFramebufferStatus, (GLenum target)) \
	X(void, Finish, ()) \
	X(void, GenVertexArrays, (GLsizei n, GLuint *arrays)) \
	X(void, DeleteVertexArrays, (GLsizei n, const GLuint *arrays)) \
	X(void, BindVertexArray, (GLuint array)) \
	X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count))

#define GOB_GL_DECLARE(ret, name, args) extern ret(GOB_GLAPI *name) args;
GOB_GL_FUNCTIONS(GOB_GL_DECLARE)
#undef GOB_GL_DECLARE

// Resolves every function; returns the name of the first missing one, or
// nullptr on success.
const char *load(void *(*getProcAddress)(const char *name));

} // namespace gob::gl
