// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/presenter.h"

#include "frontend/gl.h"
#include "gfx/png.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>

namespace gob {

namespace {

struct FilterInfo {
	VideoFilter filter;
	const char *name, *label;
};
const FilterInfo kFilters[] = {
	{VideoFilter::Nearest, "nearest", "Original (no filter)"},
	{VideoFilter::Smooth, "smooth", "Smooth (bilinear)"},
	{VideoFilter::Sharp, "sharp", "Sharp bilinear"},
	{VideoFilter::Scale2x, "scale2x", "Scale2x"},
	{VideoFilter::Scale3x, "scale3x", "Scale3x"},
	{VideoFilter::Xbr, "xbr", "Smooth edges (xBR-like)"},
	{VideoFilter::Crt, "crt", "CRT"},
};

struct MaskInfo {
	CrtMask mask;
	const char *name, *label;
};
const MaskInfo kMasks[] = {
	{CrtMask::None, "none", "None"},
	{CrtMask::ApertureGrille, "aperture", "Aperture grille (stripes)"},
	{CrtMask::SlotMask, "slot", "Slot mask"},
	{CrtMask::ShadowMask, "shadow", "Shadow mask (dots)"},
};

// Full-viewport quad generated from gl_VertexID (no vertex buffers). Every
// texture has the image's top row at t = 0: the frame is uploaded top row
// first, and passes into a render target (toTarget) are drawn so that they
// keep that layout; only the final pass into the window flips.
const char *const kVertexShader = R"(#version 150
uniform int toTarget;
out vec2 uv;
void main() {
	vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
	uv = toTarget != 0 ? p : vec2(p.x, 1.0 - p.y);
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Shared by the fragment shaders: `src` is the input texture (srcSize
// texels), the pass writes outSize pixels.
const char *const kFragmentCommon = R"(#version 150
in vec2 uv;
out vec4 color;
uniform sampler2D src;
uniform vec2 srcSize;
uniform vec2 outSize;

vec3 texel(ivec2 p) {
	return texelFetch(src, clamp(p, ivec2(0), ivec2(srcSize) - 1), 0).rgb;
}
)";

const char *const kNearestShader = R"(
void main() {
	color = vec4(texel(ivec2(floor(uv * srcSize))), 1.0);
}
)";

const char *const kSmoothShader = R"(
void main() {
	color = vec4(texture(src, uv).rgb, 1.0);
}
)";

// Sharp bilinear: as if the image were first enlarged by a large whole
// factor with nearest sampling and then bilinearly filtered to the output
// size. Within each texel the position is flattened so that the
// interpolation only happens in a band `width` output pixels wide around
// texel edges (1 pixel when softness is 0, the whole texel at 1, which is
// plain bilinear).
const char *const kSharpShader = R"(
uniform float softness;
void main() {
	vec2 scale = max(outSize / srcSize, vec2(1.0));
	vec2 width = mix(vec2(1.0), scale, softness);
	vec2 t = uv * srcSize - 0.5;
	vec2 i = floor(t);
	vec2 f = clamp((t - i - 0.5) * scale / width + 0.5, 0.0, 1.0);
	color = vec4(texture(src, (i + f + 0.5) / srcSize).rgb, 1.0);
}
)";

// Scale2x (EPX): each source pixel E becomes 2x2. With B, D, F, H the
// pixels above, left, right and below, the top-left output pixel takes D's
// colour when D and B match and the opposite neighbours differ. The other
// three outputs are the same rule with the neighbourhood mirrored.
const char *const kScale2xShader = R"(
void main() {
	vec2 p = uv * srcSize;
	ivec2 c = ivec2(floor(p));
	vec2 f = p - floor(p);
	ivec2 m = ivec2(f.x < 0.5 ? 1 : -1, f.y < 0.5 ? 1 : -1);
	vec3 E = texel(c);
	vec3 B = texel(c + ivec2(0, -m.y)), H = texel(c + ivec2(0, m.y));
	vec3 D = texel(c + ivec2(-m.x, 0)), F = texel(c + ivec2(m.x, 0));
	color = vec4(D == B && B != F && D != H ? D : E, 1.0);
}
)";

// Scale3x (AdvMAME3x): each source pixel becomes 3x3. Rules for the
// top-left corner, the top edge and the left edge output pixels (A-I is the
// 3x3 neighbourhood, E in the middle); the other corners and edges mirror
// them and the centre stays E.
const char *const kScale3xShader = R"(
void main() {
	vec2 p = uv * srcSize;
	ivec2 c = ivec2(floor(p));
	ivec2 s = ivec2(floor((p - floor(p)) * 3.0));
	ivec2 m = ivec2(s.x == 2 ? -1 : 1, s.y == 2 ? -1 : 1);
	ivec2 q = ivec2(s.x == 2 ? 0 : s.x, s.y == 2 ? 0 : s.y);
#define N(x, y) texel(c + ivec2(x, y) * m)
	vec3 A = N(-1, -1), B = N(0, -1), C = N(1, -1);
	vec3 D = N(-1, 0), E = N(0, 0), F = N(1, 0);
	vec3 G = N(-1, 1), H = N(0, 1);
	vec3 o = E;
	if (B != H && D != F) {
		if (q == ivec2(0, 0))
			o = D == B ? D : E;
		else if (q == ivec2(1, 0))
			o = (D == B && E != C) || (B == F && E != A) ? B : E;
		else if (q == ivec2(0, 1))
			o = (D == B && E != G) || (D == H && E != A) ? D : E;
	}
	color = vec4(o, 1.0);
}
)";

// Edge-directed upscaler in the spirit of xBR: for each corner of a source
// pixel, compare how much the colours change along the two diagonals of
// the surrounding 4x4 block. When the change is clearly smaller along the
// diagonal that cuts the corner off (an edge running that way) and the
// pixel differs from its neighbours there, the corner is filled with the
// neighbour's colour up to a line: 45 degrees normally, or a shallower or
// steeper slope when the edge continues one pixel further on that side.
// The fill is anti-aliased over one output pixel. Each corner is evaluated
// in a mirrored frame where it is the bottom-right one.
const char *const kXbrShader = R"(
uniform float strength;
ivec2 centre; // the source pixel under this output pixel

// The neighbour (x, y) of the centre in the corner's orientation `m`,
// fetched where it is needed: most pixels leave after E, F and H (an array
// of all 25 first was 30 times slower on a TV's Mali GPU).
vec3 at(int x, int y, ivec2 m) {
	return texel(centre + ivec2(x * m.x, y * m.y));
}

// Luma-weighted colour difference (the eye cares most about brightness).
float diff(vec3 a, vec3 b) {
	vec3 d = a - b;
	float y = dot(d, vec3(0.299, 0.587, 0.114));
	return 2.0 * abs(y) + 0.5 * abs(d.r - y) + 0.4 * abs(d.b - y);
}

bool same(vec3 a, vec3 b) {
	return diff(a, b) < 0.05;
}

void roundCorner(ivec2 m, vec2 f, float aa, inout vec3 col) {
	vec3 E = at(0, 0, m), F = at(1, 0, m), H = at(0, 1, m);
	if (same(E, F) || same(E, H))
		return;
	vec3 B = at(0, -1, m), C = at(1, -1, m);
	vec3 D = at(-1, 0, m), F4 = at(2, 0, m);
	vec3 G = at(-1, 1, m), I = at(1, 1, m), I4 = at(2, 1, m);
	vec3 H5 = at(0, 2, m), I5 = at(1, 2, m);
	// Change along the "/" diagonals around the corner, and along "\".
	float slash = diff(E, C) + diff(E, G) + diff(I, F4) + diff(I, H5) + 4.0 * diff(F, H);
	float backslash = diff(D, H) + diff(H, I5) + diff(B, F) + diff(F, I4) + 4.0 * diff(E, I);
	if (slash >= backslash)
		return;
	// Leave square shapes square: only round where the edge carries on
	// past this block rather than turning a right-angled corner.
	bool carriesOn = (!same(F, B) && !same(F, C)) || (!same(H, D) && !same(H, G)) ||
	                 (same(E, I) && ((!same(F, F4) && !same(F, I4)) || (!same(H, H5) && !same(H, I5)))) ||
	                 same(E, G) || same(E, C);
	if (!carriesOn)
		return;
	// Signed distance (in source pixels) from the edge line, positive on
	// the side that takes the neighbour's colour.
	float d = (f.x + f.y - 1.5) * 0.7071;
	if (same(G, H) && !same(D, G)) // shallow: the edge also passes below D
		d = max(d, (0.5 * f.x + f.y - 1.0) * 0.8944);
	if (same(C, F) && !same(B, C)) // steep: the edge also passes right of B
		d = max(d, (f.x + 0.5 * f.y - 1.0) * 0.8944);
	float a = clamp(d / aa + 0.5, 0.0, 1.0);
	vec3 px = diff(E, F) <= diff(E, H) ? F : H;
	col = mix(col, px, a * strength);
}

void main() {
	vec2 p = uv * srcSize;
	centre = ivec2(floor(p));
	vec2 f = p - floor(p);
	float aa = srcSize.x / outSize.x; // one output pixel
	vec3 col = texel(centre);
	roundCorner(ivec2(1, 1), f, aa, col);
	roundCorner(ivec2(-1, 1), vec2(1.0 - f.x, f.y), aa, col);
	roundCorner(ivec2(1, -1), vec2(f.x, 1.0 - f.y), aa, col);
	roundCorner(ivec2(-1, -1), 1.0 - f, aa, col);
	color = vec4(col, 1.0);
}
)";

// CRT: works in linear light (the frame is taken as a gamma 2.4 signal).
// Each of the 200 source lines is drawn as a beam with a gaussian vertical
// profile that gets wider for brighter colours, normalised so that a line
// keeps its total brightness; horizontally the beam blends neighbouring
// pixels with adjustable sharpness. Then a phosphor mask in window pixels,
// a glow from a wide blur of the frame, optional barrel curvature, a
// vignette and the display gamma.
const char *const kCrtShader = R"(
uniform float scanlines, sharpness, maskStrength, bloom, curvature, vignette, brightness, gamma;
uniform int maskType;
uniform float maskPitch;
uniform sampler2D glowMap; // kGlowShader's pass, srcSize texels

vec3 toLinear(vec3 c) {
	return pow(c, vec3(2.4));
}

vec3 scanRow(int y, float x, float hs) {
	float t = x - 0.5;
	float i = floor(t);
	float f = clamp((t - i - 0.5) * hs + 0.5, 0.0, 1.0);
	int xi = int(i);
	return mix(toLinear(texel(ivec2(xi, y))), toLinear(texel(ivec2(xi + 1, y))), f);
}

vec3 beam(vec3 c, float d) {
	vec3 w = mix(vec3(0.19), vec3(0.32), sqrt(c));
	return c * exp(-0.5 * d * d / (w * w)) / (w * 2.5066);
}

vec3 phosphors(vec2 fc) {
	vec3 m = vec3(1.0 - maskStrength);
	vec2 cell = floor(fc / maskPitch);
	float x = cell.x;
	if (maskType == 3) // shadow mask: every other row of dots shifted by half a triad
		x = floor(x + mod(cell.y, 2.0) * 1.5);
	float ph = mod(x, 3.0);
	if (ph < 0.5)
		m.r = 1.0;
	else if (ph < 1.5)
		m.g = 1.0;
	else
		m.b = 1.0;
	if (maskType == 2 && mod(cell.y + 2.0 * mod(floor(cell.x / 3.0), 2.0), 4.0) < 1.0)
		m *= 1.0 - maskStrength; // slot mask: staggered gaps between the slots
	return m;
}

void main() {
	// Barrel distortion that keeps the middle of each edge in place.
	vec2 c = uv * 2.0 - 1.0;
	float k = curvature * 0.2;
	c *= (1.0 + k * dot(c, c)) / (1.0 + k);
	vec2 w = c * 0.5 + 0.5;
	vec2 inside = min(w, 1.0 - w) * outSize;
	float edge = clamp(min(inside.x, inside.y) + 0.5, 0.0, 1.0);
	if (edge <= 0.0) {
		color = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	vec2 p = w * srcSize;
	vec2 scale = max(outSize / srcSize, vec2(1.0));
	float hs = mix(1.0, scale.x, sharpness);
	float ty = p.y - 0.5;
	float y0 = floor(ty);
	float dy = ty - y0;
	vec3 c0 = scanRow(int(y0), p.x, hs), c1 = scanRow(int(y0) + 1, p.x, hs);
	vec3 even = mix(c0, c1, clamp((dy - 0.5) * scale.y + 0.5, 0.0, 1.0));
	vec3 col = mix(even, beam(c0, dy) + beam(c1, 1.0 - dy), scanlines);

	if (bloom > 0.0) {
		vec3 g = toLinear(texture(glowMap, p / srcSize).rgb);
		col += bloom * (0.5 * max(g - col, 0.0) + 0.05 * g);
	}

	if (maskType != 0) {
		float avg = 1.0 - maskStrength * 2.0 / 3.0;
		col *= phosphors(gl_FragCoord.xy) / mix(1.0, avg, 0.5);
	}
	float v = 16.0 * w.x * (1.0 - w.x) * w.y * (1.0 - w.y);
	col *= mix(1.0, pow(v, 0.3), vignette) * brightness * edge;
	color = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / gamma)), 1.0);
}
)";

// The CRT's glow, a wide blur of the frame, worked out once per source
// pixel in a pass of its own (srcSize pixels, gamma 2.4 for the 8 bits):
// the CRT then reads it with one bilinear lookup per window pixel, where it
// summed 16 samples for every one (a TV's GPU took 50 ms a frame).
const char *const kGlowShader = R"(
vec3 toLinear(vec3 c) {
	return pow(c, vec3(2.4));
}

// A 4x4 gaussian around p, 1.8 source pixels apart; the weights are
// exp(-|o|^2 / 10) for those offsets, made constants (a TV's GPU worked
// them out for every pixel).
vec3 glow(vec2 p) {
	const float wc = 0.23270121, we = 0.44485807, wi = 0.85044120, total = 7.89143420; // corners, edges, inner four
	vec3 sum = vec3(0.0);
	for (int j = 0; j < 4; j++)
		for (int i = 0; i < 4; i++) {
			vec2 o = (vec2(float(i), float(j)) - 1.5) * 1.8;
			bool outerI = i == 0 || i == 3, outerJ = j == 0 || j == 3;
			float w = outerI && outerJ ? wc : outerI || outerJ ? we : wi;
			sum += toLinear(texture(src, (p + o) / srcSize).rgb) * w;
		}
	return sum / total;
}

void main() {
	color = vec4(pow(glow(uv * srcSize), vec3(1.0 / 2.4)), 1.0);
}
)";

// Sharp text over the final image: each output pixel finds the game pixel
// under it; where the text map has a glyph cell there, the glyph's signed
// distance field is sampled at the position inside the cell and turned into
// coverage over one output pixel, in the cell's ink colour.
const char *const kTextShader = R"(
uniform sampler2D textMap, textColors, atlas;
uniform float atlasSize, cellStride, slotsPerRow, samples, spread;
uniform vec2 frameSize, pixelSize;
void main() {
	vec2 g = uv * frameSize;
	ivec2 gp = ivec2(floor(g));
	vec4 m = texelFetch(textMap, gp, 0);
	if (m.a < 0.5)
		discard;
	float slot = floor(m.r * 255.0 + 0.5) + 256.0 * floor(m.g * 255.0 + 0.5);
	float b = floor(m.b * 255.0 + 0.5);
	vec2 local = vec2(mod(b, 16.0), floor(b / 16.0)) + (g - vec2(gp));
	vec2 origin = vec2(mod(slot, slotsPerRow), floor(slot / slotsPerRow)) * cellStride + 1.0;
	float d = (texture(atlas, (origin + local * samples) / atlasSize).r - 0.5) * 2.0 * spread;
	float px = 0.5 * (pixelSize.x + pixelSize.y); // one output pixel in glyph pixels
	float coverage = clamp(d / px + 0.5, 0.0, 1.0);
	color = vec4(texelFetch(textColors, gp, 0).rgb, coverage);
}
)";

#ifdef GOB_GLES
// OpenGL ES 3.0 (WebGL 2, webOS) takes the same shaders as GLSL ES 3.00: another first line, and
// high precision (fragment shaders default to medium floats and ints).
constexpr const char kDesktopVersion[] = "#version 150\n";
constexpr const char kEsHeader[] = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
#endif

unsigned compile(gl::GLenum type, const char *const *srcs, int count, std::string &error) {
	gl::GLuint s = gl::CreateShader(type);
#ifdef GOB_GLES
	std::vector<std::string> es(srcs, srcs + count);
	std::vector<const char *> esSrcs;
	for (std::string &src : es) {
		if (src.rfind(kDesktopVersion, 0) == 0)
			src = kEsHeader + src.substr(sizeof(kDesktopVersion) - 1);
		esSrcs.push_back(src.c_str());
	}
	gl::ShaderSource(s, count, esSrcs.data(), nullptr);
#else
	gl::ShaderSource(s, count, srcs, nullptr);
#endif
	gl::CompileShader(s);
	gl::GLint ok = 0;
	gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
	if (!ok) {
		char log[2048] = {};
		gl::GetShaderInfoLog(s, sizeof(log), nullptr, log);
		error = std::string("shader: ") + log;
		gl::DeleteShader(s);
		return 0;
	}
	return s;
}

unsigned link(const char *fragmentBody, std::string &error) {
	const char *vsrc[] = {kVertexShader};
	const char *fsrc[] = {kFragmentCommon, fragmentBody};
	gl::GLuint vs = compile(gl::VERTEX_SHADER, vsrc, 1, error);
	gl::GLuint fs = vs ? compile(gl::FRAGMENT_SHADER, fsrc, 2, error) : 0;
	if (!fs) {
		if (vs)
			gl::DeleteShader(vs);
		return 0;
	}
	gl::GLuint program = gl::CreateProgram();
	gl::AttachShader(program, vs);
	gl::AttachShader(program, fs);
	gl::LinkProgram(program);
	gl::DeleteShader(vs);
	gl::DeleteShader(fs);
	gl::GLint ok = 0;
	gl::GetProgramiv(program, gl::LINK_STATUS, &ok);
	if (!ok) {
		char log[2048] = {};
		gl::GetProgramInfoLog(program, sizeof(log), nullptr, log);
		error = std::string("link: ") + log;
		gl::DeleteProgram(program);
		return 0;
	}
	return program;
}

void setTextureParams(gl::GLint filter) {
	gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, filter);
	gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, filter);
	gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
	gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
}

void put16(std::vector<uint8_t> &b, size_t at, uint32_t v) {
	b[at] = uint8_t(v);
	b[at + 1] = uint8_t(v >> 8);
}

void put32(std::vector<uint8_t> &b, size_t at, uint32_t v) {
	put16(b, at, v);
	put16(b, at + 2, v >> 16);
}

bool writeBmp24(const std::string &path, const std::vector<uint8_t> &rgb, int w, int h) {
	// BMP rows are bottom-up, BGR, padded to 4 bytes.
	const size_t stride = (size_t(w) * 3 + 3) & ~size_t(3);
	std::vector<uint8_t> data(stride * size_t(h), 0);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			for (int c = 0; c < 3; c++)
				data[size_t(h - 1 - y) * stride + size_t(x) * 3 + size_t(2 - c)] = rgb[(size_t(y) * w + x) * 3 + c];
	std::vector<uint8_t> hdr(54, 0);
	hdr[0] = 'B';
	hdr[1] = 'M';
	put32(hdr, 2, uint32_t(54 + data.size()));
	put32(hdr, 10, 54);
	put32(hdr, 14, 40);
	put32(hdr, 18, uint32_t(w));
	put32(hdr, 22, uint32_t(h));
	put16(hdr, 26, 1);
	put16(hdr, 28, 24);
	put32(hdr, 34, uint32_t(data.size()));
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(hdr.data()), std::streamsize(hdr.size()));
	out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
	return bool(out);
}

} // namespace

const char *filterName(VideoFilter f) {
	for (const FilterInfo &i : kFilters)
		if (i.filter == f)
			return i.name;
	return "sharp";
}

const char *filterLabel(VideoFilter f) {
	for (const FilterInfo &i : kFilters)
		if (i.filter == f)
			return i.label;
	return "?";
}

const char *crtMaskName(CrtMask m) {
	for (const MaskInfo &i : kMasks)
		if (i.mask == m)
			return i.name;
	return "none";
}

const char *crtMaskLabel(CrtMask m) {
	for (const MaskInfo &i : kMasks)
		if (i.mask == m)
			return i.label;
	return "?";
}

bool parseFilter(const std::string &name, VideoFilter &f) {
	for (const FilterInfo &i : kFilters)
		if (name == i.name) {
			f = i.filter;
			return true;
		}
	return false;
}

bool parseCrtMask(const std::string &name, CrtMask &m) {
	for (const MaskInfo &i : kMasks)
		if (name == i.name) {
			m = i.mask;
			return true;
		}
	return false;
}

bool GlPresenter::init(int frameW, int frameH, std::string &error) {
	_frameW = _srcW = frameW;
	_frameH = _srcH = frameH;
	_srcScale = 1;

	const char *const bodies[kProgramCount] = {kNearestShader, kSmoothShader, kSharpShader, kScale2xShader,
	                                           kScale3xShader, kXbrShader,    kCrtShader,   kGlowShader,
	                                           kTextShader};
	for (int i = 0; i < kProgramCount; i++) {
		_programs[i] = link(bodies[i], error);
		if (!_programs[i])
			return false;
	}

	gl::GenVertexArrays(1, &_vao);
	gl::GenTextures(1, &_texture);
	gl::BindTexture(gl::TEXTURE_2D, _texture);
	setTextureParams(gl::LINEAR); // texelFetch ignores it; smooth, sharp and the CRT glow use it
	gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, frameW, frameH, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
	return true;
}

void GlPresenter::shutdown() {
	for (Target *t : {&_upscaled, &_glow}) {
		if (t->fbo)
			gl::DeleteFramebuffers(1, &t->fbo);
		if (t->texture)
			gl::DeleteTextures(1, &t->texture);
		*t = Target();
	}
	for (unsigned *t : {&_textMap, &_textColors, &_textAtlas})
		if (*t) {
			gl::DeleteTextures(1, t);
			*t = 0;
		}
	_textOn = false;
	if (_texture)
		gl::DeleteTextures(1, &_texture);
	if (_vao)
		gl::DeleteVertexArrays(1, &_vao);
	for (unsigned &p : _programs) {
		if (p)
			gl::DeleteProgram(p);
		p = 0;
	}
	_texture = _vao = 0;
}

GlPresenter::Rect GlPresenter::layout(int dw, int dh, bool aspect, bool integer) const {
	// Target shape: 320x200 with square pixels, or 320x240 (6:5 pixels).
	const double shapeW = _frameW, shapeH = aspect ? _frameH * 1.2 : _frameH;
	Rect r;
	int n = integer ? int(std::min(dw / shapeW, dh / shapeH)) : 0;
	if (n >= 1) {
		r.w = int(shapeW * n);
		r.h = int(std::lround(shapeH * n));
	} else { // fractional fit (also when the window is smaller than 1x)
		double s = std::min(dw / shapeW, dh / shapeH);
		r.w = std::max(1, int(std::lround(shapeW * s)));
		r.h = std::max(1, int(std::lround(shapeH * s)));
	}
	r.x = (dw - r.w) / 2;
	r.y = (dh - r.h) / 2;
	return r;
}

bool GlPresenter::ensureTarget(Target &t, int w, int h) {
	if (t.fbo && t.w == w && t.h == h)
		return true;
	if (!t.texture)
		gl::GenTextures(1, &t.texture);
	if (!t.fbo)
		gl::GenFramebuffers(1, &t.fbo);
	gl::BindTexture(gl::TEXTURE_2D, t.texture);
	setTextureParams(gl::LINEAR);
	gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
	gl::BindFramebuffer(gl::FRAMEBUFFER, t.fbo);
	gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, t.texture, 0);
	bool ok = gl::CheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
	gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
	t.w = ok ? w : 0;
	t.h = ok ? h : 0;
	return ok;
}

void GlPresenter::runPass(Program p, unsigned texture, int srcW, int srcH, int outW, int outH,
                          const FilterSettings &f) {
	const unsigned prog = _programs[p];
	gl::UseProgram(prog);
	auto loc = [prog](const char *name) { return gl::GetUniformLocation(prog, name); };
	gl::ActiveTexture(gl::TEXTURE0);
	gl::BindTexture(gl::TEXTURE_2D, texture);
	gl::Uniform1i(loc("src"), 0);
	gl::Uniform1i(loc("toTarget"), p == kScale2x || p == kScale3x || p == kXbr || p == kGlow);
	gl::Uniform2f(loc("srcSize"), float(srcW), float(srcH));
	gl::Uniform2f(loc("outSize"), float(outW), float(outH));
	gl::Uniform1f(loc("softness"), f.softness / 100.0f);
	gl::Uniform1f(loc("strength"), f.xbrStrength / 100.0f);
	if (p == kCrt) {
		gl::Uniform1f(loc("scanlines"), f.scanlines / 100.0f);
		gl::Uniform1f(loc("sharpness"), f.crtSharpness / 100.0f);
		gl::Uniform1i(loc("maskType"), int(f.mask));
		gl::Uniform1f(loc("maskStrength"), f.maskStrength / 100.0f);
		// About one phosphor triad per source pixel column.
		gl::Uniform1f(loc("maskPitch"), float(std::max(1L, std::lround(outW / (3.0 * _frameW)))));
		gl::Uniform1f(loc("bloom"), f.bloom / 100.0f);
		gl::Uniform1f(loc("curvature"), f.curvature / 100.0f);
		gl::Uniform1f(loc("vignette"), f.vignette / 100.0f);
		gl::Uniform1f(loc("brightness"), f.brightness / 100.0f);
		gl::Uniform1f(loc("gamma"), float(std::clamp(f.gamma, 1.0, 3.0)));
		gl::ActiveTexture(gl::TEXTURE0 + 1);
		gl::BindTexture(gl::TEXTURE_2D, _glow.texture);
		gl::Uniform1i(loc("glowMap"), 1);
		gl::ActiveTexture(gl::TEXTURE0);
	}
	gl::BindVertexArray(_vao);
	gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
	gl::BindVertexArray(0);
}

void GlPresenter::draw(const uint8_t *rgba, int dw, int dh, const Rect &vp, const FilterSettings &f) {
	gl::ActiveTexture(gl::TEXTURE0);
	gl::BindTexture(gl::TEXTURE_2D, _texture);
	gl::PixelStorei(gl::UNPACK_ALIGNMENT, 4);
	gl::TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, _srcW, _srcH, gl::RGBA, gl::UNSIGNED_BYTE, rgba);
	redraw(dw, dh, vp, f);
}

void GlPresenter::setSourceScale(int scale) {
	scale = std::max(1, scale);
	if (scale == _srcScale)
		return;
	_srcScale = scale;
	_srcW = _frameW * scale;
	_srcH = _frameH * scale;
	gl::ActiveTexture(gl::TEXTURE0);
	gl::BindTexture(gl::TEXTURE_2D, _texture);
	gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, _srcW, _srcH, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
}

void GlPresenter::upload(const uint8_t *rgba, const std::vector<Rect> &rects) {
	gl::ActiveTexture(gl::TEXTURE0);
	gl::BindTexture(gl::TEXTURE_2D, _texture);
	gl::PixelStorei(gl::UNPACK_ALIGNMENT, 4);
	gl::PixelStorei(gl::UNPACK_ROW_LENGTH, _srcW);
	for (const Rect &r : rects) {
		const int x = std::clamp(r.x, 0, _srcW), y = std::clamp(r.y, 0, _srcH);
		const int w = std::min(r.w, _srcW - x), h = std::min(r.h, _srcH - y);
		if (w <= 0 || h <= 0)
			continue;
		gl::TexSubImage2D(gl::TEXTURE_2D, 0, x, y, w, h, gl::RGBA, gl::UNSIGNED_BYTE,
		                  rgba + (size_t(y) * size_t(_srcW) + size_t(x)) * 4);
	}
	gl::PixelStorei(gl::UNPACK_ROW_LENGTH, 0);
}

void GlPresenter::redraw(int dw, int dh, const Rect &vp, const FilterSettings &f) {
	gl::Disable(gl::BLEND);
	gl::Disable(gl::DEPTH_TEST);
	gl::Disable(gl::CULL_FACE);
	gl::Disable(gl::SCISSOR_TEST);

	// Optional pixel-art upscale into the render target.
	unsigned texture = _texture;
	int srcW = _srcW, srcH = _srcH;
	Program upscaler = kProgramCount;
	int factor = 0;
	switch (_srcScale > 1 ? VideoFilter::Sharp : f.filter) { // an HD frame needs no pixel-art upscaler
	case VideoFilter::Scale2x: upscaler = kScale2x; factor = 2; break;
	case VideoFilter::Scale3x: upscaler = kScale3x; factor = 3; break;
	case VideoFilter::Xbr: // analytic, so any factor: about the output scale
		upscaler = kXbr;
		factor = std::clamp(std::min(vp.w / _frameW, vp.h / _frameH), 2, 4);
		break;
	default: break;
	}
	if (factor && ensureTarget(_upscaled, _frameW * factor, _frameH * factor)) {
		gl::BindFramebuffer(gl::FRAMEBUFFER, _upscaled.fbo);
		gl::Viewport(0, 0, _upscaled.w, _upscaled.h);
		runPass(upscaler, _texture, _frameW, _frameH, _upscaled.w, _upscaled.h, f);
		gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
		texture = _upscaled.texture;
		srcW = _upscaled.w;
		srcH = _upscaled.h;
	}

	// The CRT's glow, at the source's size.
	if (f.filter == VideoFilter::Crt && f.bloom > 0 && ensureTarget(_glow, srcW, srcH)) {
		gl::BindFramebuffer(gl::FRAMEBUFFER, _glow.fbo);
		gl::Viewport(0, 0, _glow.w, _glow.h);
		runPass(kGlow, texture, srcW, srcH, _glow.w, _glow.h, f);
		gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
	}

	gl::Viewport(0, 0, dw, dh);
	gl::ClearColor(0, 0, 0, 1);
	gl::Clear(gl::COLOR_BUFFER_BIT);
	gl::Viewport(vp.x, dh - vp.y - vp.h, vp.w, vp.h); // GL's origin is bottom left
	Program last = f.filter == VideoFilter::Nearest ? kNearest
	              : f.filter == VideoFilter::Smooth  ? kSmooth
	              : f.filter == VideoFilter::Crt     ? kCrt
	                                                 : kSharp;
	runPass(last, texture, srcW, srcH, vp.w, vp.h, f);
	if (_textOn && f.filter != VideoFilter::Crt)
		drawTextLayer(vp, dh);
	gl::Viewport(0, 0, dw, dh);
}

void GlPresenter::setTextLayer(const TextLayer &layer) {
	_textOn = layer.map != nullptr;
	if (!_textOn)
		return;
	_text = layer;
	auto upload = [&](unsigned &tex, gl::GLint filter, gl::GLenum internal, gl::GLenum format, int w, int h,
	                  const uint8_t *data, bool create) {
		if (!tex) {
			gl::GenTextures(1, &tex);
			create = true;
		}
		gl::BindTexture(gl::TEXTURE_2D, tex);
		gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
		if (create) {
			setTextureParams(filter);
			gl::TexImage2D(gl::TEXTURE_2D, 0, gl::GLint(internal), w, h, 0, format, gl::UNSIGNED_BYTE, data);
		} else {
			gl::TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, w, h, format, gl::UNSIGNED_BYTE, data);
		}
		gl::PixelStorei(gl::UNPACK_ALIGNMENT, 4);
	};
	gl::ActiveTexture(gl::TEXTURE0);
	upload(_textMap, gl::NEAREST, gl::RGBA8, gl::RGBA, _frameW, _frameH, layer.map, false);
	upload(_textColors, gl::NEAREST, gl::RGBA8, gl::RGBA, _frameW, _frameH, layer.colors, false);
	if (layer.atlasDirty || !_textAtlas || _atlasSize != layer.atlasSize) {
		upload(_textAtlas, gl::LINEAR, gl::R8, gl::RED, layer.atlasSize, layer.atlasSize, layer.atlas,
		       _atlasSize != layer.atlasSize);
		_atlasSize = layer.atlasSize;
	}
}

void GlPresenter::drawTextLayer(const Rect &vp, int dh) {
	const unsigned prog = _programs[kText];
	gl::UseProgram(prog);
	auto loc = [prog](const char *name) { return gl::GetUniformLocation(prog, name); };
	const unsigned textures[3] = {_textMap, _textColors, _textAtlas};
	const char *names[3] = {"textMap", "textColors", "atlas"};
	for (int i = 0; i < 3; i++) {
		gl::ActiveTexture(gl::TEXTURE0 + gl::GLenum(i) + 1);
		gl::BindTexture(gl::TEXTURE_2D, textures[i]);
		gl::Uniform1i(loc(names[i]), i + 1);
	}
	gl::ActiveTexture(gl::TEXTURE0);
	gl::Uniform1i(loc("toTarget"), 0);
	gl::Uniform2f(loc("frameSize"), float(_frameW), float(_frameH));
	gl::Uniform2f(loc("pixelSize"), float(_frameW) / float(std::max(vp.w, 1)), float(_frameH) / float(std::max(vp.h, 1)));
	gl::Uniform1f(loc("atlasSize"), float(_atlasSize));
	gl::Uniform1f(loc("cellStride"), float(_text.cellStride));
	gl::Uniform1f(loc("slotsPerRow"), float(_text.slotsPerRow));
	gl::Uniform1f(loc("samples"), float(_text.samples));
	gl::Uniform1f(loc("spread"), _text.spread);
	gl::Viewport(vp.x, dh - vp.y - vp.h, vp.w, vp.h);
	gl::Enable(gl::BLEND);
	gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
	gl::BindVertexArray(_vao);
	gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
	gl::BindVertexArray(0);
	gl::Disable(gl::BLEND);
}

std::vector<uint8_t> GlPresenter::readPixels(const Rect &r, int dh) {
	std::vector<uint8_t> rgb(size_t(r.w) * size_t(r.h) * 3), row(size_t(r.w) * 3);
	if (rgb.empty())
		return rgb;
	gl::ReadBuffer(gl::BACK);
	gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
#ifdef GOB_GLES
	// OpenGL ES only promises RGBA reads.
	std::vector<uint8_t> rgba(size_t(r.w) * size_t(r.h) * 4);
	gl::ReadPixels(r.x, dh - r.y - r.h, r.w, r.h, gl::RGBA, gl::UNSIGNED_BYTE, rgba.data());
	for (size_t i = 0; i < size_t(r.w) * size_t(r.h); i++)
		std::copy_n(&rgba[i * 4], 3, &rgb[i * 3]);
#else
	gl::ReadPixels(r.x, dh - r.y - r.h, r.w, r.h, gl::RGB, gl::UNSIGNED_BYTE, rgb.data());
#endif
	// GL rows are bottom-up.
	for (int y = 0; y < r.h / 2; y++) {
		uint8_t *a = &rgb[size_t(y) * row.size()], *b = &rgb[size_t(r.h - 1 - y) * row.size()];
		std::copy(a, a + row.size(), row.data());
		std::copy(b, b + row.size(), a);
		std::copy(row.begin(), row.end(), b);
	}
	return rgb;
}

bool GlPresenter::screenshot(const std::string &path, int dw, int dh) {
	std::vector<uint8_t> rgb = readPixels(Rect{0, 0, dw, dh}, dh);
	std::string lower = path;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".bmp") == 0)
		return writeBmp24(path, rgb, dw, dh);
	return writePng(path, rgb.data(), dw, dh, 3);
}

} // namespace gob
