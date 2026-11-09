// The glitch with no engine: clean at both ends, the same for the same seed, the new picture
// arriving by the end, drawing in pieces the same as drawing at once, each style its own
//
// SPDX-License-Identifier: 0BSD
#include "glitch.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace glitch;

static int failures = 0;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                \
			failures++;                                                                                                \
		}                                                                                                              \
	} while (0)

struct Image {
	int w, h;
	std::vector<uint32_t> px;
	Image(int w_, int h_, uint32_t base) : w(w_), h(h_), px(size_t(w_) * h_) {
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) px[size_t(y) * w + x] = 0xff000000u | (base ^ uint32_t(x * 3 + y * 7919 % 251));
	}
	Picture pic() const {
		Picture p;
		p.pixels = reinterpret_cast<const uint8_t *>(px.data());
		p.pitch = long(w) * 4;
		p.width = w;
		p.height = h;
		return p;
	}
};

static std::vector<uint32_t> render(const Frame &f, const Image &a, const Image &b, int pieces) {
	std::vector<uint32_t> out(size_t(a.w) * a.h, 0);
	int rows = (a.h + pieces - 1) / pieces;
	for (int top = 0; top < a.h; top += rows) {
		int h = top + rows > a.h ? a.h - top : rows;
		// in uneven parts across, as the engine may divide it
		int cut = a.w / 3 + 7;
		draw(f, a.pic(), b.pic(), 0, top, cut, h, reinterpret_cast<uint8_t *>(out.data()), long(a.w) * 4, 0, top);
		draw(f, a.pic(), b.pic(), cut, top, a.w - cut, h, reinterpret_cast<uint8_t *>(out.data()), long(a.w) * 4, cut,
		     top);
	}
	return out;
}

static size_t differ(const std::vector<uint32_t> &x, const std::vector<uint32_t> &y) {
	size_t n = 0;
	for (size_t k = 0; k < x.size(); k++) n += x[k] != y[k];
	return n;
}

// the time of the step after the one at `elapsed`, and one more moment in the same step
static void stepMoments(const Options &o, int W, int H, uint64_t elapsed, uint64_t time, uint64_t &same,
                        uint64_t &next) {
	Frame f = makeFrame(o, W, H, elapsed, time);
	same = next = elapsed;
	for (uint64_t e = elapsed + 1; e < time; e++) {
		Frame g = makeFrame(o, W, H, e, time);
		if (g.stepIndex == f.stepIndex) same = e;
		else {
			next = e;
			break;
		}
	}
}

int main() {
	const int W = 320, H = 180;
	const uint64_t T = 1000;
	Image from(W, H, 0x00204060u), to(W, H, 0x00c08040u);
	const Style styles[] = {Style::Mixed, Style::Digital, Style::Analog, Style::Mosh};
	std::vector<std::vector<uint32_t>> mids;

	for (Style st : styles) {
		Options o;
		o.seed = 42;
		o.style = st;

		// clean at the start and the end
		Frame start = makeFrame(o, W, H, 0, T);
		CHECK(start.g == 0);
		CHECK(render(start, from, to, 1) == from.px);
		Frame end = makeFrame(o, W, H, T, T);
		CHECK(end.t == 1.0);
		CHECK(render(end, from, to, 1) == to.px);

		// broken up in the middle: neither picture
		Frame mid = makeFrame(o, W, H, 500, T);
		CHECK(mid.g > 0.9);
		std::vector<uint32_t> m = render(mid, from, to, 1);
		CHECK(differ(m, from.px) > size_t(W * H / 4));
		CHECK(differ(m, to.px) > size_t(W * H / 4));
		mids.push_back(m);

		// the same seed and moment: the same picture, drawn in pieces or at once
		CHECK(render(makeFrame(o, W, H, 500, T), from, to, 7) == m);
		// another seed, another glitch
		Options o2 = o;
		o2.seed = 43;
		CHECK(render(makeFrame(o2, W, H, 500, T), from, to, 1) != m);

		// the new picture arrives by the end: every pixel late on
		Frame late = makeFrame(o, W, H, 960, T);
		int newPx = 0;
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) newPx += isNew(late, x, y);
		CHECK(newPx == W * H);
		int midNew = 0;
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) midNew += isNew(mid, x, y);
		CHECK(midNew < W * H);

		// strength 0: a cut, macroblock by macroblock, with no faults
		Options quiet = o;
		quiet.strength = 0;
		Frame q = makeFrame(quiet, W, H, 500, T);
		CHECK(q.g == 0);
		std::vector<uint32_t> qp = render(q, from, to, 1);
		bool oneOrOther = true;
		for (int y = 0; y < H && oneOrOther; y++)
			for (int x = 0; x < W; x++) {
				const Image &want = isNew(q, x, y) ? to : from;
				if (qp[size_t(y) * W + x] != want.px[size_t(y) * W + x]) {
					oneOrOther = false;
					break;
				}
			}
		CHECK(oneOrOther);

		// a still glitch (time 0) is at full strength; a picture of one alpha keeps it
		Image alpha(W, H, 0);
		for (uint32_t &c : alpha.px) c = (c & 0x00ffffffu) | 0x80000000u;
		Frame still = makeFrame(o, W, H, 0, 0);
		CHECK(still.g == 1.0);
		std::vector<uint32_t> sp = render(still, alpha, alpha, 3);
		bool alphaKept = true;
		for (uint32_t c : sp) alphaKept = alphaKept && (c >> 24) == 0x80;
		CHECK(alphaKept);
		if (st != Style::Mosh) CHECK(differ(sp, alpha.px) > 0);
	}
	// a layer coming in from nothing (its old picture transparent): pieces of the new picture show
	// before their own place has arrived, with their alpha, and the rest stays transparent
	{
		Image empty(W, H, 0), layer(W, H, 0x00c08040u);
		for (uint32_t &c : empty.px) c &= 0x00ffffffu;
		Options o;
		o.seed = 42;
		o.style = Style::Digital;
		o.damage = 100;
		int early = 0, clear = 0;
		for (uint64_t e = 300; e <= 600; e += 50) {
			Frame f = makeFrame(o, W, H, e, T);
			std::vector<uint32_t> p = render(f, empty, layer, 1);
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++) {
					if (isNew(f, x, y)) continue;
					uint32_t c = p[size_t(y) * W + x];
					if ((c >> 24) == 0xff) early++;
					else if ((c >> 24) == 0) clear++;
				}
		}
		CHECK(early > 0);
		CHECK(clear > early);
	}

	// each style its own
	for (size_t i = 0; i < mids.size(); i++)
		for (size_t j = i + 1; j < mids.size(); j++) CHECK(mids[i] != mids[j]);

	// the jumping faults hold through a step and jump at the next
	{
		Options o;
		o.seed = 42;
		o.style = Style::Digital;
		uint64_t same, next;
		stepMoments(o, W, H, 480, T, same, next);
		std::vector<uint32_t> a = render(makeFrame(o, W, H, 480, T), from, to, 1);
		if (same != 480) CHECK(render(makeFrame(o, W, H, same, T), from, to, 1) == a);
		CHECK(next != 480);
		CHECK(render(makeFrame(o, W, H, next, T), from, to, 1) != a);
	}
	// a datamosh moves on between steps: the old picture melts frame by frame
	{
		Options o;
		o.seed = 42;
		o.style = Style::Mosh;
		o.bands = o.blocks = o.jpeg = 0;
		uint64_t same, next;
		stepMoments(o, W, H, 400, T, same, next);
		Frame f = makeFrame(o, W, H, 400, T);
		CHECK(f.moshAmount > 0);
		std::vector<uint32_t> a = render(f, from, to, 1);
		CHECK(differ(a, from.px) > size_t(W * H / 8));
		if (same > 401) CHECK(render(makeFrame(o, W, H, same, T), from, to, 1) != a);
	}
	// the style's faults can be turned off one by one
	{
		Options o;
		o.seed = 9;
		o.style = Style::Mixed;
		o.bands = o.noise = o.blocks = o.damage = o.jpeg = o.mosh = o.sort = o.analog = o.flash = 0;
		o.split = 0;
		Frame f = makeFrame(o, W, H, 500, T);
		std::vector<uint32_t> p = render(f, from, to, 1);
		bool oneOrOther = true;
		for (int y = 0; y < H && oneOrOther; y++)
			for (int x = 0; x < W; x++) {
				const Image &want = isNew(f, x, y) ? to : from;
				if (p[size_t(y) * W + x] != want.px[size_t(y) * W + x]) {
					oneOrOther = false;
					break;
				}
			}
		CHECK(oneOrOther);
	}
	CHECK(styleNamed("Digital") == Style::Digital);
	CHECK(styleNamed("analog") == Style::Analog);
	CHECK(styleNamed("datamosh") == Style::Mosh);
	CHECK(styleNamed("whatever") == Style::Mixed);

	if (failures) {
		std::printf("%d failed\n", failures);
		return 1;
	}
	std::printf("glitch_test: all passed\n");
	return 0;
}
