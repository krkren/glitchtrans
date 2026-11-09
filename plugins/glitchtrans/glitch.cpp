// glitch.cpp - see glitch.h
//
// SPDX-License-Identifier: 0BSD
#include "glitch.h"

#include <algorithm>
#include <cmath>

namespace glitch {

namespace {

// splitmix64: a good hash of a few numbers
uint64_t mix(uint64_t x) {
	x += 0x9e3779b97f4a7c15ull;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
	return x ^ (x >> 31);
}

uint64_t hash3(uint64_t a, uint64_t b, uint64_t c) { return mix(mix(mix(a) ^ b) ^ c); }

struct Rng {
	uint64_t s;
	explicit Rng(uint64_t seed) : s(seed) {}
	uint64_t next() { return s = mix(s); }
	double unit() { return double(next() >> 11) * (1.0 / 9007199254740992.0); } // [0, 1)
	int range(int lo, int hi) { // [lo, hi]
		if (hi <= lo) return lo;
		return lo + int(next() % uint64_t(hi - lo + 1));
	}
};

inline uint32_t channelSwap(uint32_t c) { // BGRA: blue and red trade places
	return (c & 0xff00ff00u) | ((c & 0xffu) << 16) | ((c >> 16) & 0xffu);
}
inline uint32_t invert(uint32_t c) { return (c & 0xff000000u) | (~c & 0x00ffffffu); }
inline uint32_t posterize(uint32_t c) { return (c & 0xff000000u) | (c & 0x00c0c0c0u) | ((c & 0x00c0c0c0u) >> 2); }

inline int wrap(int x, int w) {
	x %= w;
	return x < 0 ? x + w : x;
}
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

} // namespace

Frame makeFrame(const Options &o, int width, int height, uint64_t elapsed, uint64_t time) {
	Frame f;
	f.width = width;
	f.height = height;
	f.seed = o.seed;
	f.spread = clampi(o.spread, 0, 100);
	int step = o.step > 0 ? o.step : 50;
	f.stepIndex = uint32_t(elapsed / uint64_t(step));
	double strength = clampi(o.strength, 0, 100) / 100.0;
	if (time == 0) {
		f.t = 0.5;
		f.g = strength;
	} else {
		f.t = elapsed >= time ? 1.0 : double(elapsed) / double(time);
		// rises to the middle and falls again; a little flat at the top
		f.g = strength * std::pow(std::sin(3.14159265358979 * f.t), 0.75);
	}
	if (width <= 0 || height <= 0) return f;
	if (f.g <= 0.0001) {
		f.bandEnd.push_back(height);
		f.bandShift.push_back(0);
		return f;
	}

	Rng r(hash3(o.seed, f.stepIndex, 0x67c1));
	int maxShift = o.shift >= 0 ? o.shift : width / 12;
	int maxSplit = o.split >= 0 ? o.split : std::max(1, width / 96);

	// bands: cut points, sorted
	int bands = clampi(o.bands, 1, 400);
	std::vector<int> cuts;
	for (int k = 0; k < bands - 1; k++) cuts.push_back(r.range(1, height - 1));
	std::sort(cuts.begin(), cuts.end());
	cuts.push_back(height);
	for (int end : cuts) {
		if (!f.bandEnd.empty() && end <= f.bandEnd.back()) continue;
		f.bandEnd.push_back(end);
		// most bands keep still when the glitch is weak
		int s = 0;
		if (r.unit() < 0.25 + 0.6 * f.g) {
			double u = r.unit() * 2 - 1;
			s = int(u * u * u * maxShift * f.g * 1.6);
		}
		f.bandShift.push_back(s);
	}

	// the colours apart, now this way, now that
	double u = r.unit() * 2 - 1;
	f.splitX = int(std::round((u < 0 ? -1 : 1) * (0.35 + 0.65 * std::fabs(u)) * maxSplit * f.g));

	f.noiseRate = int(clampi(o.noise, 0, 100) / 100.0 * 0.18 * f.g * 65536);

	int nBlocks = int(std::round(clampi(o.blocks, 0, 1000) * f.g * (0.5 + r.unit())));
	for (int k = 0; k < nBlocks; k++) {
		Frame::Block b;
		b.w = r.range(std::max(4, width / 40), std::max(8, width / 5));
		b.h = r.range(std::max(2, height / 90), std::max(4, height / 12));
		b.x = r.range(0, std::max(0, width - b.w));
		b.y = r.range(0, std::max(0, height - b.h));
		b.dx = int((r.unit() * 2 - 1) * width / 6);
		b.dy = int((r.unit() * 2 - 1) * height / 10);
		double e = r.unit();
		b.effect = e < 0.45 ? 0 : e < 0.7 ? 1 : e < 0.85 ? 3 : 2;
		f.blocks.push_back(b);
	}
	return f;
}

bool rowIsNew(const Frame &f, int y) {
	if (f.t >= 1.0) return true;
	if (f.t <= 0.0) return false;
	// each stripe of 8 rows arrives at its own moment around the middle
	double h = double(hash3(f.seed, uint64_t(y >> 3), 0x5719) >> 11) * (1.0 / 9007199254740992.0);
	double at = 0.5 + (h - 0.5) * (f.spread / 100.0);
	return f.t >= at;
}

void draw(const Frame &f, const Picture &from, const Picture &to, int left, int top, int w, int h, uint8_t *dest,
          long destPitch, int destLeft, int destTop) {
	const int W = f.width, H = f.height;
	if (W <= 0 || H <= 0) return;
	size_t band = 0;
	for (int yy = 0; yy < h; yy++) {
		int y = top + yy;
		uint32_t *out = reinterpret_cast<uint32_t *>(dest + destPitch * (destTop + yy)) + destLeft;
		while (band + 1 < f.bandEnd.size() && y >= f.bandEnd[band]) band++;
		if (band > 0 && y < f.bandEnd[band - 1]) { // rows given out of order: look again
			band = 0;
			while (band + 1 < f.bandEnd.size() && y >= f.bandEnd[band]) band++;
		}
		const Picture &src = rowIsNew(f, y) ? to : from;

		// four rows of static (thin rows read as lines on a big screen)
		if (f.noiseRate > 0 && int(hash3(f.seed, f.stepIndex, uint64_t(y >> 2) + 0x10000) & 0xffff) < f.noiseRate) {
			uint64_t s = hash3(f.seed ^ 0xa5a5, f.stepIndex, uint64_t(y));
			uint32_t tint = uint32_t(hash3(s, 1, 2));
			for (int xx = 0; xx < w; xx++) {
				s = s * 6364136223846793005ull + 1442695040888963407ull;
				uint32_t v = uint32_t(s >> 56);
				uint32_t r = (v * (0xc0 + (tint & 0x3f))) >> 8, g = (v * (0xc0 + ((tint >> 8) & 0x3f))) >> 8,
				         b = (v * (0xc0 + ((tint >> 16) & 0x3f))) >> 8;
				uint32_t a = src.at(clampi(left + xx, 0, W - 1), y) & 0xff000000u;
				out[xx] = a | (r << 16) | (g << 8) | b;
			}
			continue;
		}

		int shift = f.bandShift.empty() ? 0 : f.bandShift[band];
		int sx = f.splitX;
		for (int xx = 0; xx < w; xx++) {
			int x = wrap(left + xx - shift, W);
			uint32_t g = src.at(x, y);
			if (sx == 0) {
				out[xx] = g;
				continue;
			}
			uint32_t rr = src.at(clampi(x - sx, 0, W - 1), y);
			uint32_t bb = src.at(clampi(x + sx, 0, W - 1), y);
			out[xx] = (g & 0xff00ff00u) | (rr & 0x00ff0000u) | (bb & 0x000000ffu);
		}

		// blocks over this row, drawn on top
		for (const Frame::Block &b : f.blocks) {
			if (y < b.y || y >= b.y + b.h) continue;
			int x0 = std::max(b.x, left), x1 = std::min(b.x + b.w, left + w);
			int fy = clampi(y + b.dy, 0, H - 1);
			for (int x = x0; x < x1; x++) {
				uint32_t c = src.at(wrap(x + b.dx, W), fy);
				switch (b.effect) {
				case 1: c = channelSwap(c); break;
				case 2: c = invert(c); break;
				case 3: c = posterize(c); break;
				default: break;
				}
				out[x - left] = c;
			}
		}
	}
}

} // namespace glitch
