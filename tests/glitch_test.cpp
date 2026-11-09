// The glitch with no engine: clean at both ends, the same for the same seed, the new picture
// arriving by the end, and drawing in pieces the same as drawing at once
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
		// in two halves across, as the engine may divide it
		int half = a.w / 2;
		draw(f, a.pic(), b.pic(), 0, top, half, h, reinterpret_cast<uint8_t *>(out.data()), long(a.w) * 4, 0, top);
		draw(f, a.pic(), b.pic(), half, top, a.w - half, h, reinterpret_cast<uint8_t *>(out.data()), long(a.w) * 4,
		     half, top);
	}
	return out;
}

static size_t differ(const std::vector<uint32_t> &x, const std::vector<uint32_t> &y) {
	size_t n = 0;
	for (size_t k = 0; k < x.size(); k++) n += x[k] != y[k];
	return n;
}

int main() {
	const int W = 320, H = 180;
	Image from(W, H, 0x00204060u), to(W, H, 0x00c08040u);
	Options o;
	o.seed = 42;

	// clean at the start and the end
	Frame start = makeFrame(o, W, H, 0, 1000);
	CHECK(start.g == 0);
	CHECK(render(start, from, to, 1) == from.px);
	Frame end = makeFrame(o, W, H, 1000, 1000);
	CHECK(end.t == 1.0);
	CHECK(render(end, from, to, 1) == to.px);

	// broken up in the middle
	Frame mid = makeFrame(o, W, H, 500, 1000);
	CHECK(mid.g > 0.9);
	std::vector<uint32_t> m = render(mid, from, to, 1);
	CHECK(differ(m, from.px) > size_t(W * H / 4));
	CHECK(differ(m, to.px) > size_t(W * H / 4));
	CHECK(!mid.blocks.empty());
	CHECK(mid.bandEnd.size() > 1);
	CHECK(mid.bandEnd.back() == H);

	// the same seed and moment: the same picture, drawn in pieces or at once
	Frame again = makeFrame(o, W, H, 500, 1000);
	CHECK(render(again, from, to, 7) == m);
	// within one step nothing moves; the next step jumps
	Frame sameStep = makeFrame(o, W, H, 510, 1000);
	CHECK(sameStep.stepIndex == mid.stepIndex);
	Frame nextStep = makeFrame(o, W, H, 560, 1000);
	CHECK(nextStep.stepIndex == mid.stepIndex + 1);
	CHECK(render(nextStep, from, to, 1) != m);
	// another seed, another glitch
	Options o2 = o;
	o2.seed = 43;
	CHECK(render(makeFrame(o2, W, H, 500, 1000), from, to, 1) != m);

	// strength 0: a cut in stripes, no faults
	Options quiet = o;
	quiet.strength = 0;
	Frame q = makeFrame(quiet, W, H, 500, 1000);
	CHECK(q.g == 0);
	std::vector<uint32_t> qp = render(q, from, to, 1);
	for (int y = 0; y < H; y++) {
		const uint32_t *row = &qp[size_t(y) * W];
		const uint32_t *want = rowIsNew(q, y) ? &to.px[size_t(y) * W] : &from.px[size_t(y) * W];
		if (std::memcmp(row, want, size_t(W) * 4) != 0) {
			CHECK(!"a row of a quiet glitch isn't one picture or the other");
			break;
		}
	}

	// the new picture arrives stripe by stripe: some rows by the middle, every row by the end
	int newRows = 0;
	for (int y = 0; y < H; y++) newRows += rowIsNew(mid, y);
	CHECK(newRows > 0 && newRows < H);
	Frame late = makeFrame(o, W, H, 950, 1000);
	int lateRows = 0;
	for (int y = 0; y < H; y++) lateRows += rowIsNew(late, y);
	CHECK(lateRows == H);

	// a still glitch (time 0) is at full strength and keeps the alpha
	Image alpha(W, H, 0);
	for (uint32_t &c : alpha.px) c = (c & 0x00ffffffu) | 0x80000000u;
	Frame still = makeFrame(o, W, H, 0, 0);
	CHECK(still.g == 1.0);
	std::vector<uint32_t> sp = render(still, alpha, alpha, 3);
	bool alphaKept = true;
	for (uint32_t c : sp) alphaKept = alphaKept && (c >> 24) == 0x80;
	CHECK(alphaKept);
	CHECK(differ(sp, alpha.px) > 0);

	if (failures) {
		std::printf("%d failed\n", failures);
		return 1;
	}
	std::printf("glitch_test: all passed\n");
	return 0;
}
