// glitch.cpp - see glitch.h
//
// SPDX-License-Identifier: 0BSD
#include "glitch.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace glitch {

namespace {

const double kPi = 3.14159265358979323846;

// splitmix64: a good hash of a few numbers
uint64_t mix(uint64_t x) {
	x += 0x9e3779b97f4a7c15ull;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
	return x ^ (x >> 31);
}

uint64_t hash3(uint64_t a, uint64_t b, uint64_t c) { return mix(mix(mix(a) ^ b) ^ c); }
double unitOf(uint64_t h) { return double(h >> 11) * (1.0 / 9007199254740992.0); } // [0, 1)
double unit3(uint64_t a, uint64_t b, uint64_t c) { return unitOf(hash3(a, b, c)); }

struct Rng {
	uint64_t s;
	explicit Rng(uint64_t seed) : s(seed) {}
	uint64_t next() { return s = mix(s); }
	double unit() { return unitOf(next()); }
	int range(int lo, int hi) { // [lo, hi]
		if (hi <= lo) return lo;
		return lo + int(next() % uint64_t(hi - lo + 1));
	}
	double sym() { return unit() * 2 - 1; } // [-1, 1)
};

inline int wrap(int x, int w) {
	x %= w;
	return x < 0 ? x + w : x;
}
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
inline double smooth(double a, double b, double x) {
	double t = clampd((x - a) / (b - a), 0, 1);
	return t * t * (3 - 2 * t);
}

// v clamped to 0-255 by a table, for v in [-512, 1024)
struct ClampTable {
	uint8_t v[1536];
	ClampTable() {
		for (int i = 0; i < 1536; i++) v[i] = uint8_t(clampi(i - 512, 0, 255));
	}
};
const ClampTable kClamp;
inline uint32_t cl(int v) { return kClamp.v[clampi(v, -512, 1023) + 512]; }

inline int R(uint32_t c) { return int((c >> 16) & 0xff); }
inline int G(uint32_t c) { return int((c >> 8) & 0xff); }
inline int B(uint32_t c) { return int(c & 0xff); }
inline uint32_t rgb(int r, int g, int b) { return (cl(r) << 16) | (cl(g) << 8) | cl(b); }
inline int luma(uint32_t c) { return (R(c) * 77 + G(c) * 150 + B(c) * 29) >> 8; }

inline uint32_t channelSwap(uint32_t c) { // BGRA: blue and red trade places
	return (c & 0xff00ff00u) | ((c & 0xffu) << 16) | ((c >> 16) & 0xffu);
}
inline uint32_t invert(uint32_t c) { return (c & 0xff000000u) | (~c & 0x00ffffffu); }

// KinoGlitch's channel shuffle: green and red trade places, lifted where the pixel is dark
inline uint32_t shuffle(uint32_t c) {
	int k = (255 - (R(c) + G(c) + B(c))) / 2;
	return rgb(G(c) + k, R(c) + k, B(c) + k);
}

const int kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

// four levels a channel, ordered dither
inline uint32_t crush(uint32_t c, int x, int y) {
	int d = (kBayer[((y & 3) << 2) | (x & 3)] - 8) * 85 / 16;
	auto q = [&](int v) { return clampi((clampi(v + d, 0, 255) + 42) / 85 * 85, 0, 255); };
	return rgb(q(R(c)), q(G(c)), q(B(c)));
}

// DCT basis: cos((2x + 1) u pi / 16), times 1024
struct CosTable {
	int v[8][8];
	CosTable() {
		for (int u = 0; u < 8; u++)
			for (int x = 0; x < 8; x++) v[u][x] = int(std::lround(std::cos((2 * x + 1) * u * kPi / 16) * 1024));
	}
};
const CosTable kCos;

struct Defaults {
	int bands, noise, blocks, damage, jpeg, mosh, sort, analog, flash;
};

Defaults defaultsOf(Style s) {
	switch (s) {
	case Style::Digital: return {18, 25, 12, 70, 75, 0, 45, 0, 35};
	case Style::Analog: return {10, 35, 0, 0, 0, 0, 0, 100, 15};
	case Style::Mosh: return {3, 0, 2, 0, 25, 100, 0, 0, 0};
	default: return {14, 15, 8, 30, 45, 70, 30, 25, 25};
	}
}

// value noise: a smooth random field over the picture, 0-1
struct Field {
	static const int NX = 7, NY = 5;
	double v[NY][NX];
	Field(uint64_t seed, uint64_t salt) {
		for (int j = 0; j < NY; j++)
			for (int i = 0; i < NX; i++) v[j][i] = unit3(seed, uint64_t(j * NX + i), salt);
	}
	double at(double fx, double fy) const { // fx, fy 0-1
		double x = clampd(fx, 0, 1) * (NX - 1), y = clampd(fy, 0, 1) * (NY - 1);
		int ix = std::min(int(x), NX - 2), iy = std::min(int(y), NY - 2);
		double ax = x - ix, ay = y - iy;
		ax = ax * ax * (3 - 2 * ax);
		ay = ay * ay * (3 - 2 * ay);
		return (v[iy][ix] * (1 - ax) + v[iy][ix + 1] * ax) * (1 - ay) + (v[iy + 1][ix] * (1 - ax) + v[iy + 1][ix + 1] * ax) * ay;
	}
};

// a step's length: about `step` ms, now shorter, now longer, now and then held much longer
uint64_t stepLength(uint32_t seed, uint32_t k, int step) {
	double u = unit3(seed, k, 0x57e9);
	double m = 0.35 + 1.6 * u * u;
	if (unit3(seed, k, 0x10ad) > 0.93) m *= 3;
	return std::max<uint64_t>(1, uint64_t(step * m));
}

// how strong the glitch is through the transition: up, held, down, with hiccups on either side
double envelope(uint32_t seed, uint32_t k, double t) {
	if (t <= 0 || t >= 1) return 0;
	double e = smooth(0, 0.3, t) * (1 - smooth(0.72, 1.0, t));
	if ((t > 0.03 && t < 0.3) || (t > 0.72 && t < 0.88)) {
		if (unit3(seed, k, 0xb11b) < 0.2) e = std::max(e, 0.5 + 0.45 * unit3(seed, k, 0xb22b));
	}
	return e;
}

// the step whose faults the slower ones still show: they're drawn again only now and then
uint32_t generationOf(uint32_t seed, uint32_t k) {
	for (uint32_t j = k;; j--) {
		if (j == 0 || k - j >= 24 || unit3(seed, j, 0x6e4e) < 0.3) return j;
	}
}

} // namespace

Style styleNamed(const std::string &name) {
	std::string s;
	for (char c : name) s += char(std::tolower(static_cast<unsigned char>(c)));
	if (s == "digital") return Style::Digital;
	if (s == "analog" || s == "analogue") return Style::Analog;
	if (s == "mosh" || s == "datamosh") return Style::Mosh;
	return Style::Mixed;
}

Frame makeFrame(const Options &o, int width, int height, uint64_t elapsed, uint64_t time) {
	Frame f;
	f.width = width;
	f.height = height;
	f.seed = o.seed;
	const Defaults d = defaultsOf(o.style);
	auto pick = [](int v, int def) { return v >= 0 ? v : def; };
	int step = o.step > 0 ? o.step : 50;
	double strength = clampi(o.strength, 0, 100) / 100.0;
	f.still = time == 0;
	if (f.still) {
		f.stepIndex = uint32_t(elapsed / uint64_t(step));
		f.t = f.tStep = 0.5;
		f.g = strength;
	} else {
		if (elapsed > time) elapsed = time;
		uint32_t k = 0;
		uint64_t acc = 0;
		while (k < 1000000) {
			uint64_t len = stepLength(o.seed, k, step);
			if (acc + len > elapsed) break;
			acc += len;
			k++;
		}
		f.stepIndex = k;
		f.t = elapsed >= time ? 1.0 : double(elapsed) / double(time);
		f.tStep = elapsed >= time ? 1.0 : double(acc) / double(time);
		f.g = strength * envelope(o.seed, k, f.tStep);
	}
	f.generation = f.still ? f.stepIndex : generationOf(o.seed, f.stepIndex);
	if (width <= 0 || height <= 0) return f;
	const int W = width, H = height;
	const uint32_t seed = o.seed, k = f.stepIndex, gen = f.generation;
	// the glitch surges and eases from step to step
	f.pulse = f.still ? 1.0 : 0.35 + 0.65 * std::pow(unit3(seed, k, 0x9175), 0.6);
	const double g = f.g * f.pulse;

	f.wBands = g;
	f.wNoise = clampi(pick(o.noise, d.noise), 0, 100) / 100.0 * g;
	f.wGarbage = (o.style == Style::Digital || o.style == Style::Mixed) ? f.wNoise * 0.6 : 0;
	f.wAnalog = clampi(pick(o.analog, d.analog), 0, 100) / 100.0 * g;
	f.moshAmount = clampi(pick(o.mosh, d.mosh), 0, 100) / 100.0 * strength;
	// the slower faults take turns: one leads each generation, the others join now and then
	{
		double w[4] = {clampi(pick(o.damage, d.damage), 0, 100) / 100.0, clampi(pick(o.jpeg, d.jpeg), 0, 100) / 100.0,
		               clampi(pick(o.sort, d.sort), 0, 100) / 100.0, 0};
		double sum = w[0] + w[1] + w[2];
		int lead = -1;
		if (sum > 0) {
			double u = unit3(seed, gen, 0x1ead) * sum;
			lead = u < w[0] ? 0 : u < w[0] + w[1] ? 1 : 2;
		}
		bool on[3];
		for (int i = 0; i < 3; i++) on[i] = w[i] > 0 && (i == lead || unit3(seed, gen, 0x10e + uint64_t(i)) < w[i] * 0.35);
		f.wDamage = on[0] ? w[0] * g : 0;
		f.wJpeg = on[1] ? w[1] * g : 0;
		f.wSort = on[2] ? w[2] * g : 0;
	}

	// ---- arrival of the new picture, by macroblock (the same through the transition) ----
	f.mb = clampi(int(std::lround(W / 64.0)), 4, 32) & ~1;
	f.mbw = (W + f.mb - 1) / f.mb;
	f.mbh = (H + f.mb - 1) / f.mb;
	const double spread = clampi(o.spread, 0, 100) / 100.0;
	const double centre = o.style == Style::Mosh ? 0.6 : 0.5;
	const size_t nmb = size_t(f.mbw) * f.mbh;
	f.arrive.resize(nmb);
	{
		Field patches(seed, 0xa3);
		for (int by = 0; by < f.mbh; by++) {
			double hr = unit3(seed, uint64_t(by / 2), 0xa1); // stripes two macroblocks high
			double ku = unit3(seed, uint64_t(by / 2), 0xa77);
			bool rightward = unit3(seed, uint64_t(by / 2), 0xa78) < 0.5;
			// how a stripe arrives: whole, in patches, or sweeping across
			int kind;
			switch (o.style) {
			case Style::Analog: kind = 0; break;
			case Style::Mosh: kind = 1; break;
			case Style::Digital: kind = ku < 0.6 ? 0 : 2; break;
			default: kind = ku < 0.3 ? 0 : ku < 0.8 ? 1 : 2; break;
			}
			double jitter = o.style == Style::Mosh ? 0.08 : o.style == Style::Analog ? 0 : 0.04;
			for (int bx = 0; bx < f.mbw; bx++) {
				size_t mi = size_t(by) * f.mbw + bx;
				double across = f.mbw > 1 ? double(bx) / (f.mbw - 1) : 0;
				double h;
				if (kind == 0) h = hr;
				else if (kind == 1) h = patches.at(across, f.mbh > 1 ? double(by) / (f.mbh - 1) : 0);
				else h = hr * 0.4 + (rightward ? across : 1 - across) * 0.6;
				h += (unit3(seed, mi, 0xa2) - 0.5) * 2 * jitter;
				f.arrive[mi] = float(clampd(centre + (h - 0.5) * spread, 0.02, 0.94));
			}
		}
	}

	// ---- the datamosh's motion (the same through the transition, so it moves smoothly) ----
	if (f.moshAmount > 0) {
		f.moshStart = 0.08 + 0.12 * unit3(seed, 0, 0x305);
		Field fa(seed, 0x307), fm(seed, 0x308);
		double theta0 = unit3(seed, 0, 0x306) * 2 * kPi;
		double maxMag = W * 0.2 * f.moshAmount;
		f.mvx.resize(nmb);
		f.mvy.resize(nmb);
		f.mbMode.resize(nmb);
		for (int by = 0; by < f.mbh; by++)
			for (int bx = 0; bx < f.mbw; bx++) {
				size_t mi = size_t(by) * f.mbw + bx;
				double fx = f.mbw > 1 ? double(bx) / (f.mbw - 1) : 0, fy = f.mbh > 1 ? double(by) / (f.mbh - 1) : 0;
				double th = theta0 + (fa.at(fx, fy) - 0.5) * 3.0;
				double mag = maxMag * (0.25 + 0.75 * fm.at(fx, fy)) * (0.85 + 0.3 * unit3(seed, mi, 0x309));
				f.mvx[mi] = float(std::cos(th) * mag);
				f.mvy[mi] = float(std::sin(th) * mag * 0.55);
				double u = unit3(seed, mi, 0x30a);
				f.mbMode[mi] = uint8_t(u < 0.15 ? 1 : u < 0.82 ? 0 : u < 0.93 ? 2 : 4);
			}
	}

	// ---- this step's faults ----
	Rng r(hash3(seed, k, 0x67c1));
	int maxShift = o.shift >= 0 ? o.shift : W / 12;
	int maxSplit = o.split >= 0 ? o.split : std::max(1, W / 96);

	// bands, on most steps
	int bands = clampi(pick(o.bands, d.bands), 0, 400);
	bool bandsOn = g > 0.0001 && bands > 1 && r.unit() < 0.3 + 0.65 * g;
	if (bandsOn) {
		std::vector<int> cuts;
		for (int c = 0; c < bands - 1; c++) cuts.push_back(r.range(1, H - 1));
		std::sort(cuts.begin(), cuts.end());
		cuts.push_back(H);
		for (int end : cuts) {
			if (!f.bandEnd.empty() && end <= f.bandEnd.back()) continue;
			f.bandEnd.push_back(end);
			int kind = 0, s = 0, stretch = 256;
			if (r.unit() < 0.2 + 0.6 * g) {
				double u = o.style == Style::Analog ? 0 : r.unit(); // a signal's rows only slide
				if (u < 0.82) {
					double v = r.sym();
					s = int(v * v * v * maxShift * g * 1.6);
				} else if (u < 0.93) {
					kind = 1;
				} else {
					kind = 2;
					s = r.range(0, W - 1);
					stretch = int(256 * (1.5 + 2.5 * r.unit()));
				}
			}
			f.bandShift.push_back(s);
			f.bandKind.push_back(uint8_t(kind));
			f.bandStretch.push_back(stretch);
		}
	} else {
		f.bandEnd.push_back(H);
		f.bandShift.push_back(0);
		f.bandKind.push_back(0);
		f.bandStretch.push_back(256);
	}

	if (g > 0.0001) {
		// the colours apart, now this way, now that
		if (r.unit() < 0.4 + 0.5 * g) {
			double u = r.sym();
			f.splitX = int(std::round((u < 0 ? -1 : 1) * (0.35 + 0.65 * std::fabs(u)) * maxSplit * g));
		}
		f.noiseRate = int(f.wNoise * 0.12 * 65536);
		f.garbageRate = int(f.wGarbage * 0.06 * 65536);

		// displaced rectangles
		int nBlocks = int(std::round(clampi(pick(o.blocks, d.blocks), 0, 1000) * g * (0.3 + r.unit())));
		for (int b = 0; b < nBlocks; b++) {
			Frame::Block bl;
			bl.w = r.range(std::max(4, W / 40), std::max(8, W / 5));
			bl.h = r.range(std::max(2, H / 90), std::max(4, H / 12));
			bl.x = r.range(0, std::max(0, W - bl.w));
			bl.y = r.range(0, std::max(0, H - bl.h));
			bl.dx = int(r.sym() * W / 6);
			bl.dy = int(r.sym() * H / 10);
			double e = r.unit();
			bl.effect = e < 0.45 ? 0 : e < 0.7 ? 1 : e < 0.85 ? 3 : 2;
			f.blocks.push_back(bl);
		}
	}

	// the analog signal: rows that jitter and a shake on every step; roll, slant on some
	const double wa = f.wAnalog;
	if (wa > 0) {
		f.jitterThresh = int(clampd(1 - wa * 0.8, 0, 1) * 65536);
		f.jitterAmp = std::max(1, int(W * (0.002 + wa * wa * wa * 0.02)));
		f.shakeX = int(r.sym() * W * 0.02 * wa);
		Rng ra(hash3(seed, gen, 0xa4a1));
		if (ra.unit() < wa * 0.35) {
			f.roll = int(H * (0.15 + 0.7 * ra.unit()));
			f.rollBar = std::max(2, H / 22);
		}
		if (ra.unit() < wa * 0.3) {
			f.skew = (ra.unit() < 0.5 ? -1 : 1) * (0.08 + 0.42 * ra.unit()) * wa;
			f.skewY = ra.range(0, H - 1);
		}
		f.trackH = std::max(4, int(H / 12.0 * (0.5 + wa)));
		double pos = unit3(seed, 0, 0x7a7) + f.t * 1.3;
		pos -= std::floor(pos);
		f.trackY = int(pos * (H + f.trackH)) - f.trackH;
		f.chromaLag = std::max(1, int(std::lround(W / 320.0 * (1 + 3 * wa))));
		f.driftAmp = W * 0.004 * wa;
		f.driftPhase = unit3(seed, k, 0xd71f) * 2 * kPi;
		f.scanline = int(48 * wa);
		f.interlace = true;
	}

	// the damaged cells: KinoGlitch's runs over a grid
	if (f.wDamage > 0.001) {
		f.cols = 64;
		f.rows = std::max(1, int(std::lround(64.0 * H / W)));
		f.cellW = (W + f.cols - 1) / f.cols;
		f.cellH = (H + f.rows - 1) / f.rows;
		size_t n = size_t(f.cols) * f.rows;
		f.cells.assign(n, 0);
		f.cellDX.assign(n, 0);
		f.cellDY.assign(n, 0);
		Rng rc(hash3(seed, gen, 0xd4d4));
		double th = 1.001 - f.wDamage * 0.75;
		double vx = rc.unit(), vy = rc.unit(), vz = rc.unit(), vw = rc.unit();
		for (size_t c = 0; c < n; c++) {
			if (rc.unit() > 0.89) {
				vx = rc.unit();
				vy = rc.unit();
				vz = rc.unit();
				vw = rc.unit();
			}
			uint8_t fl = 0;
			if (std::pow(vz, 2.5) > th) fl |= 1;
			if (std::pow(vw, 2.5) > th) fl |= 2;
			if (std::pow(vz, 3.5) > th) fl |= 4;
			if (std::pow(vw * vx, 2.0) > th) fl |= 8;
			f.cells[c] = fl;
			f.cellDX[c] = int(vx * W);
			f.cellDY[c] = int((vy - 0.5) * H * 0.3);
		}
		f.cellRowOn.assign(size_t(f.rows), 0);
		for (size_t c = 0; c < n; c++)
			if (f.cells[c]) f.cellRowOn[c / size_t(f.cols)] = 1;
	}

	// the broken JPEG stream
	if (f.wJpeg > 0.05) {
		f.jbw = (W + 7) / 8;
		f.jbh = (H + 7) / 8;
		int total = f.jbw * f.jbh;
		Rng rj(hash3(seed, gen, 0x1b9e));
		int n = std::max(1, std::min(5, int(std::round(f.wJpeg * (0.5 + 3 * rj.unit())))));
		double s = std::min(1.0, f.wJpeg * 1.5);
		for (int c = 0; c < n; c++) {
			Frame::Corrupt cr{};
			cr.b0 = rj.range(0, total - 1);
			cr.b1 = rj.unit() < 0.35 ? total : std::min(total, cr.b0 + f.jbw * rj.range(1, 18));
			cr.shift = rj.unit() < 0.5 ? 0 : (rj.unit() < 0.7 ? 1 : -1) * rj.range(1, std::max(1, f.jbw / 2));
			double dy = rj.sym() * 70 * s, dcb = rj.sym() * 90 * s, dcr = rj.sym() * 90 * s;
			cr.dr = int(dy + 1.402 * dcr);
			cr.dg = int(dy - 0.344 * dcb - 0.714 * dcr);
			cr.db = int(dy + 1.772 * dcb);
			if (rj.unit() < 0.45) {
				cr.u = rj.range(0, 7);
				cr.v = rj.range(cr.u == 0 ? 1 : 0, 7);
				cr.amp = int((18 + 42 * rj.unit()) * s);
			}
			if (rj.unit() < 0.12) {
				cr.lost = true;
				cr.b1 = std::min(total, cr.b0 + rj.range(std::max(1, f.jbw / 4), f.jbw * 2));
			}
			f.corrupt.push_back(cr);
		}
	}

	// pixel sorting
	if (f.wSort > 0.05) {
		Rng rs(hash3(seed, gen, 0x5027));
		int n = std::max(1, std::min(4, int(std::round(f.wSort * 3 * (0.4 + rs.unit())))));
		for (int c = 0; c < n; c++) {
			Frame::SortBand sb;
			int h = rs.range(std::max(1, H / 40), std::max(2, H / 6));
			sb.y0 = rs.range(0, std::max(0, H - h));
			sb.y1 = sb.y0 + h;
			double u = rs.unit();
			if (u < 0.6) {
				sb.lo = rs.range(90, 150);
				sb.hi = 256;
			} else if (u < 0.85) {
				sb.lo = 0;
				sb.hi = rs.range(70, 130);
			} else {
				sb.lo = rs.range(40, 90);
				sb.hi = rs.range(160, 220);
			}
			sb.desc = rs.unit() < 0.5;
			f.sorts.push_back(sb);
		}
	}

	// a frame of signal failure, at the height
	int flash = clampi(pick(o.flash, d.flash), 0, 100);
	if (!f.still && f.g > 0.55 && f.pulse > 0.6 && unit3(seed, k, 0xf1a5) < flash / 100.0 * 0.16) {
		f.flash = 1 + int(unit3(seed, k, 0xf1a6) * 4);
		f.flashChannel = int(unit3(seed, k, 0xf1a7) * 3);
	}

	// ---- each macroblock now ----
	f.mbFlags.assign(nmb, 0);
	f.mbOX.assign(nmb, 0);
	f.mbOY.assign(nmb, 0);
	f.mbMix.assign(nmb, 0);
	for (int by = 0; by < f.mbh; by++)
		for (int bx = 0; bx < f.mbw; bx++) {
			size_t mi = size_t(by) * f.mbw + bx;
			double a = f.arrive[mi];
			bool nw = !f.still && (f.t >= 1.0 || (f.t > 0 && f.tStep >= a));
			uint8_t fl = nw ? Frame::MbNew : 0;
			if (nw && f.interlace && f.t < 1.0 && f.tStep - a < 0.06) fl |= Frame::MbField;
			if (f.moshAmount > 0 && !f.still && f.t < 1.0) {
				uint8_t mode = f.mbMode[mi];
				if (!nw && f.t > f.moshStart && a > f.moshStart) {
					double m = clampd((f.t - f.moshStart) / (a - f.moshStart), 0, 1);
					f.mbMix[mi] = uint8_t(m * 255);
					fl |= Frame::MbMosh;
					if (mode == 3 && (hash3(seed, k, mi + 0x40000) & 3) == 0) fl |= Frame::MbLost;
					else if (mode != 2) {
						// follow the block back along the motion: where the field stands still, it stops
						double dm = std::pow(m, 1.4);
						double cx = (bx + 0.5) * f.mb, cy = (by + 0.5) * f.mb;
						const int N = 10;
						for (int s = 0; s < N; s++) {
							int qx = clampi(int(cx) / f.mb, 0, f.mbw - 1), qy = clampi(int(cy) / f.mb, 0, f.mbh - 1);
							size_t qi = size_t(qy) * f.mbw + qx;
							if (s > 0 && f.mbMode[qi] == 2) break;
							cx = clampd(cx - f.mvx[qi] * dm / N, 0, W - 1);
							cy = clampd(cy - f.mvy[qi] * dm / N, 0, H - 1);
						}
						f.mbOX[mi] = int(std::lround(cx - (bx + 0.5) * f.mb));
						f.mbOY[mi] = int(std::lround(cy - (by + 0.5) * f.mb));
						if (mode == 1) fl |= Frame::MbSmear;
					}
				} else if (nw && mode == 4 && f.tStep - a < 0.05) {
					// arrives a little out of place, then snaps in
					double left = 1 - (f.tStep - a) / 0.05;
					f.mbOX[mi] = int(f.mvx[mi] * 0.1 * left);
					f.mbOY[mi] = int(f.mvy[mi] * 0.1 * left);
				}
			}
			f.mbFlags[mi] = fl;
		}

	// ---- each row as read ----
	if (g > 0.0001) {
		f.rowY.resize(size_t(H));
		f.rowDX.resize(size_t(H));
		f.rowStretch.resize(size_t(H));
		f.rowDrift.assign(size_t(H), 0);
		size_t band = 0;
		for (int sy = 0; sy < H; sy++) {
			int y1 = f.roll ? wrap(sy + f.roll, H) : sy;
			int dx = 0;
			if (f.jitterAmp) {
				int v = int(hash3(seed, k, uint64_t(y1) + 0x30000) & 0xffff) - 32768;
				if (std::abs(v) * 2 >= f.jitterThresh) dx += v * f.jitterAmp / 32768;
				dx += f.shakeX;
				if (f.skew != 0 && y1 > f.skewY) dx += int(f.skew * (y1 - f.skewY));
			}
			int stretch = -1;
			if (f.bandEnd.size() > 1) {
				band = size_t(std::upper_bound(f.bandEnd.begin(), f.bandEnd.end(), y1) - f.bandEnd.begin());
				if (band >= f.bandEnd.size()) band = f.bandEnd.size() - 1;
				switch (f.bandKind[band]) {
				case 1: y1 = band > 0 ? f.bandEnd[band - 1] : 0; break;
				case 2: stretch = int(band); break;
				default: dx -= f.bandShift[band]; break;
				}
			}
			f.rowY[size_t(sy)] = y1;
			f.rowDX[size_t(sy)] = dx;
			f.rowStretch[size_t(sy)] = stretch;
			if (f.driftAmp > 0) f.rowDrift[size_t(sy)] = int(std::sin(y1 * 0.045 + f.driftPhase) * f.driftAmp);
		}
	}
	return f;
}

bool isNew(const Frame &f, int x, int y) {
	if (f.still) return false;
	if (f.t >= 1.0) return true;
	if (f.t <= 0.0 || f.mbw <= 0) return false;
	int bx = clampi(x / f.mb, 0, f.mbw - 1), by = clampi(y / f.mb, 0, f.mbh - 1);
	return f.tStep >= f.arrive[size_t(by) * f.mbw + bx];
}

namespace {

// x brought back inside [0, w) (it's never more than a few widths out)
inline int wrapNear(int x, int w) {
	while (x < 0) x += w;
	while (x >= w) x -= w;
	return x;
}

// the picture's pixel at (x, y): x wraps round, y stays inside
inline uint32_t pix(const Picture &p, int x, int y) {
	return p.at(wrapNear(x, p.width), clampi(y, 0, p.height - 1));
}

// one row's pixels [x0, x1) into out[0 ..]: in spans over which nothing changes but x
void drawRow(const Frame &f, const Picture &from, const Picture &to, int y, int x0, int x1, uint32_t *out) {
	const int W = f.width, H = f.height;
	const uint32_t seed = f.seed, k = f.stepIndex;
	const int mb = f.mb;
	const int mby = std::min(y / mb, f.mbh - 1);
	const size_t mbRow = size_t(mby) * f.mbw;
	const bool rows = !f.rowY.empty();

	// ---- whole rows of something else ----
	if (rows) {
		bool rollBar = f.roll && wrap(y + f.roll, H) >= H - f.rollBar;
		bool staticRow = f.noiseRate > 0 && int(hash3(seed, k, uint64_t(y >> 2) + 0x10000) & 0xffff) < f.noiseRate;
		int gh = std::max(2, H / 120);
		bool garbageRow = !staticRow && f.garbageRate > 0 &&
		                  int(hash3(seed, k, uint64_t(y / gh) + 0x20000) & 0xffff) < f.garbageRate;
		if (rollBar || staticRow || garbageRow) {
			uint64_t s = hash3(seed ^ 0xa5a5, k, uint64_t(y) * 65536 + uint64_t(x0));
			uint32_t tint = uint32_t(hash3(seed, k, uint64_t(y >> 2)));
			int segW = std::max(4, W / 64);
			for (int x = x0; x < x1; x++) {
				uint8_t fl = f.mbFlags[mbRow + size_t(std::min(x / mb, f.mbw - 1))];
				uint32_t alpha = ((fl & Frame::MbNew) ? to : from).at(x, y) & 0xff000000u;
				s = s * 6364136223846793005ull + 1442695040888963407ull;
				uint32_t c;
				if (rollBar) {
					uint32_t v = uint32_t(s >> 59);
					c = (v << 16) | (v << 8) | v;
				} else if (staticRow) {
					uint32_t v = uint32_t(s >> 56);
					c = rgb(int(v * (0xc0 + (tint & 0x3f))) >> 8, int(v * (0xc0 + ((tint >> 8) & 0x3f))) >> 8,
					        int(v * (0xc0 + ((tint >> 16) & 0x3f))) >> 8);
				} else {
					// bars of colour, as memory read as a picture
					uint64_t h = hash3(seed ^ 0x6a6a, k, uint64_t(y / gh) * 4096 + uint64_t(x / segW));
					c = uint32_t(h);
					if ((h >> 40) & 1) c = (c & 0x00e0e0e0u) | ((c & 0x00e0e0e0u) >> 3);
				}
				out[x - x0] = alpha | (c & 0x00ffffffu);
			}
			return;
		}
	}

	// ---- what this row has ----
	const Frame::Corrupt *jr[8];
	int nj = 0;
	if (!f.corrupt.empty()) {
		int rowB0 = (y >> 3) * f.jbw, rowB1 = rowB0 + f.jbw;
		for (const Frame::Corrupt &c : f.corrupt)
			if (c.b0 < rowB1 && c.b1 > rowB0 && nj < 8) jr[nj++] = &c;
	}
	const bool inTrack = f.trackH > 0 && y >= f.trackY && y < f.trackY + f.trackH;
	const int trackDX = inTrack ? int((hash3(seed, k, uint64_t(y) + 0x31000) & 0xff) - 128) * f.jitterAmp * 3 / 128 : 0;
	const Frame::Block *rb[64];
	int nb = 0;
	for (const Frame::Block &b : f.blocks)
		if (y >= b.y && y < b.y + b.h && nb < 64) rb[nb++] = &b;
	const int cellY = f.cells.empty() ? 0 : std::min(y / f.cellH, f.rows - 1);
	const bool cellsHere = !f.cells.empty() && f.cellRowOn[size_t(cellY)];
	const size_t cellRow = size_t(cellY) * f.cols;
	const int streakW = std::max(8, W / 48);
	uint8_t streak[64] = {};
	if (inTrack)
		for (int i = 0; i < 64; i++) {
			uint64_t h = hash3(seed ^ 0x7c7c, k, uint64_t(y) * 64 + uint64_t(i));
			if ((h & 0xff) < 40) streak[i] = uint8_t(((h >> 8) & 0x7f) + 128);
		}
	const int scan = (f.scanline > 0 && (y & 1)) ? 256 - f.scanline : 256;
	const bool rowColour = f.chromaLag || f.splitX || inTrack || scan != 256 || f.flash;

	// where the spans break: macroblocks, cells, rectangles, the JPEG stream's ranges
	int cuts[512];
	int nc = 0;
	auto cut = [&](int x) {
		if (x > x0 && x < x1 && nc < 510) cuts[nc++] = x;
	};
	for (int x = (x0 / mb + 1) * mb; x < x1; x += mb) cut(x);
	if (cellsHere)
		for (int x = (x0 / f.cellW + 1) * f.cellW; x < x1; x += f.cellW) cut(x);
	for (int j = 0; j < nb; j++) {
		cut(rb[j]->x);
		cut(rb[j]->x + rb[j]->w);
	}
	const int rowB0 = (y >> 3) * f.jbw;
	for (int j = 0; j < nj; j++) {
		cut((jr[j]->b0 - rowB0) * 8);
		cut((jr[j]->b1 - rowB0) * 8);
	}
	cuts[nc++] = x1;
	std::sort(cuts, cuts + nc);

	int a = x0;
	for (int ci = 0; ci < nc; ci++) {
		int b = cuts[ci];
		if (b <= a) continue;
		// ---- this span's constants ----
		const int bx = std::min(a / mb, f.mbw - 1);
		const size_t mi = mbRow + size_t(bx);
		const uint8_t mfl = f.mbFlags[mi];
		const bool newHere = (mfl & Frame::MbNew) != 0;
		const Picture &home = newHere ? to : from;

		// the JPEG stream over this span
		int dcR = 0, dcG = 0, dcB = 0, acAmp = 0, acU = 0, acV = 0, jshift = 0;
		bool lost = false;
		if (nj) {
			int blk = rowB0 + (a >> 3);
			for (int j = 0; j < nj; j++) {
				const Frame::Corrupt &c = *jr[j];
				if (blk < c.b0 || blk >= c.b1) continue;
				lost = lost || c.lost;
				jshift += c.shift;
				dcR += c.dr;
				dcG += c.dg;
				dcB += c.db;
				if (c.amp) {
					acAmp = c.amp;
					acU = c.u;
					acV = c.v;
				}
			}
		}
		if (lost) {
			uint32_t c = rgb(128 + dcR / 4, 128 + dcG / 4, 128 + dcB / 4);
			for (int x = a; x < b; x++) out[x - x0] = (home.at(x, y) & 0xff000000u) | c;
			a = b;
			continue;
		}
		if (jshift) {
			// the stream slid along: the source row changes where the slide crosses a row of blocks,
			// so split the span there
			int q = (a >> 3) - jshift; // the source block's column, before wrapping into rows
			int rowStep = q >= 0 ? q / f.jbw : -((-q + f.jbw - 1) / f.jbw);
			int nextCol = (rowStep + 1) * f.jbw + jshift; // the first block column of the next source row
			int split = nextCol * 8;
			if (split > a && split < b) {
				// draw [a, split) now, the rest on the next pass
				ci--;
				b = split;
			}
		}

		// ---- the mapping: sx = x + dx (or stretched), sy fixed ----
		int sy = y, dx = 0;
		if (jshift) {
			int q = (a >> 3) - jshift;
			int total = f.jbw * f.jbh;
			int bs = wrap(rowB0 + q, total);
			sy = std::min(H - 1, (bs / f.jbw) * 8 + (y & 7));
			dx = (bs % f.jbw) * 8 - (a & ~7);
		}
		int stretchBand = -1, driftDX = 0;
		if (rows) {
			dx += f.rowDX[size_t(sy)] + trackDX;
			stretchBand = f.rowStretch[size_t(sy)];
			driftDX = f.rowDrift[size_t(sy)];
			sy = f.rowY[size_t(sy)];
		}
		bool other = false, shuf = false, crushed = false;
		if (cellsHere) {
			size_t cidx = cellRow + size_t(std::min(a / f.cellW, f.cols - 1));
			uint8_t fl = f.cells[cidx];
			if (fl & 1) {
				dx += f.cellDX[cidx];
				sy = wrap(sy + f.cellDY[cidx], H);
			}
			other = (fl & 2) != 0;
			shuf = (fl & 4) != 0;
			crushed = (fl & 8) != 0;
		}
		int rectEffect = -1;
		for (int j = 0; j < nb; j++) {
			const Frame::Block &bl = *rb[j];
			if (a < bl.x || a >= bl.x + bl.w) continue;
			dx = bl.dx;
			sy = clampi(y + bl.dy, 0, H - 1);
			stretchBand = -1;
			rectEffect = bl.effect;
		}

		// which picture, and the datamosh
		bool showNew = newHere != other;
		if ((mfl & Frame::MbField) && (y & 1)) showNew = other; // the old field, for a moment
		const Picture &src = showNew ? to : from;
		const bool moshing = (mfl & Frame::MbMosh) && !newHere && !other;
		if (moshing && (mfl & Frame::MbLost)) {
			for (int x = a; x < b; x++) out[x - x0] = (home.at(x, y) & 0xff000000u) | 0x00008700u;
			a = b;
			continue;
		}
		const bool smear = moshing && (mfl & Frame::MbSmear);
		int smearX0 = 0, smearX1 = 0;
		if (smear) {
			int bx0 = bx * mb, by0 = mby * mb;
			smearX0 = bx0;
			smearX1 = std::min(W, bx0 + mb) - 1;
			sy += clampi(y + f.mbOY[mi], by0, std::min(H, by0 + mb) - 1) - y;
		} else if (moshing || (newHere && !other)) {
			dx += f.mbOX[mi];
			sy += f.mbOY[mi];
		}
		sy = clampi(sy, 0, H - 1);
		const int ox = f.mbOX[mi];
		const int mix = moshing ? f.mbMix[mi] : 0;
		const int stretchX = stretchBand >= 0 ? f.bandShift[size_t(stretchBand)] : 0;
		const int stretchBy = stretchBand >= 0 ? f.bandStretch[size_t(stretchBand)] : 256;
		const bool colour = rowColour || driftDX || moshing || rectEffect >= 0 || shuf || crushed || dcR || dcG ||
		                    dcB || acAmp;
		const uint32_t *srow = reinterpret_cast<const uint32_t *>(src.pixels + src.pitch * sy);
		const uint32_t *hrow = reinterpret_cast<const uint32_t *>(home.pixels + home.pitch * y);
		const uint32_t *trow0 = reinterpret_cast<const uint32_t *>(to.pixels + to.pitch * sy);
		const uint32_t *trow1 = reinterpret_cast<const uint32_t *>(to.pixels + to.pitch * std::min(sy + 1, H - 1));
		const int kr = mix * 3 / 2, mixIn = mix * mix * 77 / 65025;

		// ---- the span ----
		if (!colour && !smear && stretchBand < 0) {
			// moved, no more: copied in runs that don't wrap
			int x = a;
			while (x < b) {
				int sx = wrapNear(x + dx, W);
				int n = std::min(b - x, W - sx);
				uint32_t *o = out + (x - x0);
				const uint32_t *sp = srow + sx, *hp = hrow + x;
				if (sp == hp) std::copy(sp, sp + n, o);
				else
					for (int i = 0; i < n; i++) o[i] = (hp[i] & 0xff000000u) | (sp[i] & 0x00ffffffu);
				x += n;
			}
			a = b;
			continue;
		}
		for (int x = a; x < b; x++) {
			int sx;
			if (stretchBand >= 0) {
				sx = stretchX + int(int64_t(x + dx - stretchX) * 256 / stretchBy);
			} else
				sx = x + dx;
			if (smear) sx += clampi(x + ox, smearX0, smearX1) - x;
			sx = wrapNear(sx, W);
			uint32_t c = srow[sx];
			const uint32_t alpha = hrow[x] & 0xff000000u;
			if (!colour) {
				out[x - x0] = alpha | (c & 0x00ffffffu);
				continue;
			}
			if (f.chromaLag) {
				uint32_t c2 = srow[wrapNear(sx - f.chromaLag, W)];
				int dl = luma(c) - luma(c2);
				c = rgb(R(c2) + dl, G(c2) + dl, B(c2) + dl);
			}
			if (f.splitX) {
				uint32_t rr = srow[wrapNear(sx - f.splitX, W)], bb = srow[wrapNear(sx + f.splitX, W)];
				c = (c & 0x0000ff00u) | (rr & 0x00ff0000u) | (bb & 0x000000ffu);
			}
			if (driftDX) c = (c & 0x00ff00ffu) | (srow[wrapNear(sx + driftDX, W)] & 0x0000ff00u);
			if (moshing) {
				// the new picture's edges come through on the old one's colours, and its colours seep in
				uint32_t t0 = trow0[sx], tr = trow0[sx + 1 < W ? sx + 1 : sx], td = trow1[sx];
				int r0 = R(t0), g0 = G(t0), b0 = B(t0);
				int er = 2 * r0 - R(tr) - R(td), eg = 2 * g0 - G(tr) - G(td), eb = 2 * b0 - B(tr) - B(td);
				c = rgb((R(c) * (256 - mixIn) + r0 * mixIn + er * kr) >> 8, (G(c) * (256 - mixIn) + g0 * mixIn + eg * kr) >> 8,
				        (B(c) * (256 - mixIn) + b0 * mixIn + eb * kr) >> 8);
			}
			switch (rectEffect) {
			case 1: c = channelSwap(c); break;
			case 2: c = invert(c); break;
			case 3: c = crush(c, x, y); break;
			default: break;
			}
			if (shuf) c = shuffle(c);
			if (crushed) c = crush(c, x, y);
			if (dcR || dcG || dcB || acAmp) {
				int ac = acAmp ? acAmp * kCos.v[acU][x & 7] / 1024 * kCos.v[acV][y & 7] / 1024 : 0;
				c = rgb(R(c) + dcR + ac, G(c) + dcG + ac, B(c) + dcB + ac);
			}
			if (inTrack) {
				// white streaks across the tracking band
				int v = streak[std::min(x / streakW, 63)];
				if (v) c = rgb(std::max(R(c), v), std::max(G(c), v), std::max(B(c), v));
			}
			if (scan != 256) c = rgb(R(c) * scan >> 8, G(c) * scan >> 8, B(c) * scan >> 8);
			switch (f.flash) {
			case 1: c = invert(c); break;
			case 2: c = f.flashChannel == 0 ? (c & 0x00ff0000u) : f.flashChannel == 1 ? (c & 0x0000ff00u) : (c & 0x000000ffu); break;
			case 3: c = luma(c) >= 110 ? 0x00ffffffu : 0; break;
			case 4: {
				uint32_t t0 = pix(to, x, y), t1 = pix(to, x + 2, y), t2 = pix(to, x, y + 2);
				int e = std::min(255, (std::abs(luma(t0) - luma(t1)) + std::abs(luma(t0) - luma(t2))) * 3);
				c = rgb(e, e, e);
				break;
			}
			default: break;
			}
			out[x - x0] = alpha | (c & 0x00ffffffu);
		}
		a = b;
	}
}

// pixel sorting: runs whose brightness is in the band's range, sorted by brightness
void sortRow(const Frame::SortBand &sb, uint32_t *row, int w) {
	std::vector<std::pair<int, uint32_t>> run;
	int x = 0;
	while (x < w) {
		int l = luma(row[x]);
		if (l < sb.lo || l >= sb.hi) {
			x++;
			continue;
		}
		int start = x;
		while (x < w) {
			int v = luma(row[x]);
			if (v < sb.lo || v >= sb.hi) break;
			x++;
		}
		if (x - start < 3) continue;
		run.clear();
		for (int i = start; i < x; i++) run.emplace_back(luma(row[i]), row[i]);
		if (sb.desc)
			std::stable_sort(run.begin(), run.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
		else
			std::stable_sort(run.begin(), run.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		// the alpha stays where it was
		for (int i = start; i < x; i++) row[i] = (row[i] & 0xff000000u) | (run[size_t(i - start)].second & 0x00ffffffu);
	}
}

} // namespace

void draw(const Frame &f, const Picture &from, const Picture &to, int left, int top, int w, int h, uint8_t *dest,
          long destPitch, int destLeft, int destTop) {
	const int W = f.width, H = f.height;
	if (W <= 0 || H <= 0 || w <= 0 || h <= 0) return;
	std::vector<uint32_t> whole;
	for (int yy = 0; yy < h; yy++) {
		int y = top + yy;
		uint32_t *out = reinterpret_cast<uint32_t *>(dest + destPitch * (destTop + yy)) + destLeft;
		const Frame::SortBand *sb = nullptr;
		for (const Frame::SortBand &s : f.sorts)
			if (y >= s.y0 && y < s.y1) sb = &s;
		if (!sb) {
			drawRow(f, from, to, y, left, left + w, out);
			continue;
		}
		// a sorted row is drawn whole, so every piece of it sorts the same
		whole.resize(size_t(W));
		drawRow(f, from, to, y, 0, W, whole.data());
		sortRow(*sb, whole.data(), W);
		std::copy(whole.begin() + left, whole.begin() + left + w, out);
	}
}

} // namespace glitch
