// glitch.h - the glitch picture, with no engine in it: what the transition and GlitchTrans.apply
// draw, and what the tests check
//
// A glitch is drawn from one or two pictures (the old and the new, 32-bit BGRA, as the engine's
// layers hold them) at a moment of the transition. It changes in jumps, not smoothly: every
// `step` ms a new set of faults is drawn from the seed, as a broken signal would show. Its
// strength rises from nothing at the start to its height in the middle and falls to nothing at
// the end, so the first and last frames are the pictures as they are. The faults:
//
//   bands   the picture cut into horizontal bands of random heights, each shifted sideways
//           (the picture wraps round), some not at all
//   split   the red and blue channels sampled apart from the green, sideways
//   noise   rows of static in place of the picture
//   blocks  rectangles copied from elsewhere, some with their channels swapped, inverted or
//           reduced to a few colours
//
// and the new picture arrives in stripes of 8 rows, each at its own moment around the middle.
//
// SPDX-License-Identifier: 0BSD
#pragma once

#include <cstdint>
#include <vector>

namespace glitch {

struct Options {
	uint32_t seed = 1;     ///< the same seed draws the same glitch
	int strength = 100;    ///< 0-100: how strong at its height
	int bands = 18;        ///< bands the picture is cut into
	int shift = -1;        ///< the most a band moves sideways, px (-1: a twelfth of the width)
	int split = -1;        ///< the most the colours move apart, px (-1: width / 96)
	int noise = 30;        ///< 0-100: how many rows of static
	int blocks = 12;       ///< displaced rectangles at the height
	int step = 50;         ///< ms between jumps
	int spread = 60;       ///< 0-100: how far apart (of the whole time) the stripes of the new picture arrive
};

/// A picture: rows top first, `pitch` bytes apart
struct Picture {
	const uint8_t *pixels = nullptr;
	long pitch = 0;
	int width = 0, height = 0;
	uint32_t at(int x, int y) const {
		return *reinterpret_cast<const uint32_t *>(pixels + pitch * y + long(x) * 4);
	}
};

/// One moment's faults
struct Frame {
	int width = 0, height = 0;
	double t = 0;          ///< 0-1 through the transition
	double g = 0;          ///< the strength now, 0-1
	uint32_t seed = 0, stepIndex = 0;
	int spread = 60;
	int splitX = 0;        ///< red sampled at +splitX, blue at -splitX
	std::vector<int> bandEnd;   ///< each band's last row + 1, top first
	std::vector<int> bandShift; ///< each band's sideways shift
	int noiseRate = 0;     ///< out of 65536: four rows are static
	struct Block {
		int x, y, w, h;    ///< where it's drawn
		int dx, dy;        ///< where from, relative
		int effect;        ///< 0 copy, 1 channels swapped, 2 inverted, 3 few colours
	};
	std::vector<Block> blocks;
};

/// The faults at `elapsed` ms of a transition of `time` ms (time 0: a still glitch at full
/// strength, at step `elapsed / step`)
Frame makeFrame(const Options &o, int width, int height, uint64_t elapsed, uint64_t time);

/// Whether row y shows the new picture at this moment
bool rowIsNew(const Frame &f, int y);

/// Draws the rectangle (left, top, w, h) of the frame into dest (at destLeft, destTop): from
/// `from` and `to` (the same picture for a still glitch; both the frame's size)
void draw(const Frame &f, const Picture &from, const Picture &to, int left, int top, int w, int h, uint8_t *dest,
          long destPitch, int destLeft, int destTop);

} // namespace glitch
