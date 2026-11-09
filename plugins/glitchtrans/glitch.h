// glitch.h - the glitch picture, with no engine in it: what the transition and GlitchTrans.apply
// draw, and what the tests check
//
// A glitch is drawn from one or two pictures (the old and the new, 32-bit BGRA, as the engine's
// layers hold them) at a moment of the transition. It is made of the faults a broken digital or
// analog picture shows, layered and staged over the transition's time:
//
//   bands     the picture cut into horizontal bands, each shifted sideways (wrapping round),
//             stretched from a point, or smeared down from its first row
//   split     the red and blue channels sampled apart from the green
//   noise     rows of static, and on digital styles rows of "memory garbage" (bars of colour)
//   blocks    rectangles copied from elsewhere, some with their channels swapped or inverted
//   damage    a grid of cells, damaged in runs (as a decoder loses a stretch of its data): a cell
//             shows another part of the picture, the other picture, or its colours shuffled or
//             crushed to a few with a dither. The runs hold for a while and are drawn again
//             at irregular moments
//   jpeg      a broken JPEG stream: from a point of the picture on (in 8x8 blocks, in reading
//             order) the colours drift (the DC values are coded as differences, so one bad value
//             tints everything after it), the blocks slide along by a few, a frequency pattern
//             shows in some, and a stretch of blocks may be lost (grey)
//   mosh      a datamosh: the new picture's first frame is lost, so its motion is applied to the
//             old picture: the old picture's macroblocks slide and smear along a motion field,
//             the new picture's edges come through on them, and each macroblock is refreshed
//             into the new picture at its own moment (some arriving a little out of place and
//             snapping in)
//   sort      pixel sorting: in a few bands, runs of pixels whose brightness is in a range are
//             sorted by brightness
//   analog    a bad analog signal: rows that jitter, a picture that shakes, rolls (with the
//             black bar of the vertical blanking) or slants (horizontal hold), a tracking band
//             with white streaks moving down, the colour lagging behind the brightness and
//             drifting, scan lines, and the new picture arriving in interlaced fields
//   flash     single frames of signal failure at the height: inverted, one channel, a hard
//             black and white, or the new picture's outline lit up
//
// The glitch jumps, as a broken signal does: its faults change at steps of irregular length
// (about `step` ms; some hold much longer), and the slower ones (damage, jpeg, sort, the analog
// events) only now and then. A datamosh moves smoothly, as it would on screen. The strength
// rises from nothing, holds through the middle and falls to nothing at the end, with a few
// hiccups before and after, so the first and last frames are the pictures as they are.
//
// The techniques follow public effects whose licences allow it (all MIT), reimplemented here:
// keijiro's KinoGlitch (the damage grid and its thresholds, row jitter, colour drift),
// gl-transitions' GlitchMemories and GlitchDisplace (per-block channel offsets, the flash) and
// glitch-canvas (corrupting a JPEG's scan data); the datamosh and the JPEG faults follow how
// video and JPEG decoders fail.
//
// SPDX-License-Identifier: 0BSD
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace glitch {

/// Which faults lead (each fault's own option can still change it)
enum class Style {
	Mixed,   ///< everything, staged: the default
	Digital, ///< damaged data: blocks, a broken JPEG, sorting, garbage rows
	Analog,  ///< a bad signal: jitter, roll, slant, tracking, colour lag, scan lines
	Mosh,    ///< a datamosh: the old picture melts along the new one's motion
};

/// "mixed", "digital", "analog" or "mosh" (any case); anything else is Mixed
Style styleNamed(const std::string &name);

struct Options {
	uint32_t seed = 1;     ///< the same seed draws the same glitch
	int strength = 100;    ///< 0-100: how strong at its height
	Style style = Style::Mixed;
	// -1 for each: the style's own
	int bands = -1;        ///< bands the picture is cut into
	int shift = -1;        ///< the most a band moves sideways, px (-1: a twelfth of the width)
	int split = -1;        ///< the most the colours move apart, px (-1: width / 96)
	int noise = -1;        ///< 0-100: rows of static and garbage
	int blocks = -1;       ///< displaced rectangles at the height
	int damage = -1;       ///< 0-100: the damaged cells
	int jpeg = -1;         ///< 0-100: the broken JPEG stream
	int mosh = -1;         ///< 0-100: the datamosh
	int sort = -1;         ///< 0-100: the pixel sorting
	int analog = -1;       ///< 0-100: the bad analog signal
	int flash = -1;        ///< 0-100: the frames of signal failure
	int step = 50;         ///< about how many ms a jump of the glitch holds
	int spread = 60;       ///< 0-100: how far apart (of the whole time) the parts of the new picture arrive
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
	double tStep = 0;      ///< t at the start of this step: what the jumping faults go by
	double g = 0;          ///< the strength now, 0-1
	uint32_t seed = 0, stepIndex = 0, generation = 0;
	bool still = false;    ///< a still glitch (GlitchTrans.apply): one picture

	// how strong each fault is now, 0-1 (the option times the strength now)
	double wBands = 0, wNoise = 0, wGarbage = 0, wDamage = 0, wJpeg = 0, wSort = 0, wAnalog = 0;
	double moshAmount = 0; ///< the datamosh's own amount (the strength, not the envelope)

	// arrival of the new picture, by macroblock
	int mb = 16, mbw = 0, mbh = 0;
	std::vector<float> arrive;  ///< t at which each macroblock shows the new picture

	// the datamosh
	double moshStart = 0.3;
	std::vector<float> mvx, mvy; ///< each macroblock's motion, px per unit of the mosh's progress
	std::vector<uint8_t> mbMode; ///< 0 slide, 1 smear, 2 stuck, 3 flickers lost, 4 slides then arrives out of place

	// each macroblock now: what it shows and where from (worked out once a frame)
	enum { MbNew = 1, MbMosh = 2, MbLost = 4, MbSmear = 8, MbField = 16 };
	std::vector<uint8_t> mbFlags;
	std::vector<int> mbOX, mbOY; ///< added to where its pixels are read
	std::vector<uint8_t> mbMix;  ///< how far the mosh has gone, 0-255

	// each row of the picture as read (after a JPEG slide): where it's read from
	std::vector<int> rowY;       ///< the row read (the roll, a band's smear)
	std::vector<int> rowDX;      ///< added to x (jitter, shake, slant, a band's shift)
	std::vector<int> rowStretch; ///< a band stretched from a point: its index, or -1
	std::vector<int> rowDrift;   ///< the green channel's drift

	// bands
	std::vector<int> bandEnd;   ///< each band's last row + 1, top first
	std::vector<int> bandShift; ///< each band's sideways shift
	std::vector<uint8_t> bandKind; ///< 0 shift, 1 smear down from its first row, 2 stretch from bandShift's x
	std::vector<int> bandStretch;  ///< 2: by how much, in 1/256 (256 none)
	int splitX = 0;        ///< red sampled at +splitX, blue at -splitX

	// rows
	double pulse = 1;      ///< this step's share of the strength: the glitch surges and eases
	int noiseRate = 0;     ///< out of 65536: groups of four rows of static
	int garbageRate = 0;   ///< out of 65536: groups of rows of garbage
	int jitterThresh = 65536; ///< out of 65536: a row jitters when its random value is past it
	int jitterAmp = 0;     ///< the most a jittering row moves, px
	int shakeX = 0;        ///< the whole picture's sideways shake
	int roll = 0, rollBar = 0;  ///< vertical hold: rows read `roll` further down, a bar at the seam
	double skew = 0;       ///< horizontal hold: px a row moves per row below skewY
	int skewY = 0;
	int trackY = 0, trackH = 0; ///< the tracking band
	int chromaLag = 0;     ///< the colour read this far to the left of the brightness
	double driftAmp = 0, driftPhase = 0; ///< the green channel's wavy drift
	int scanline = 0;      ///< out of 256: how much darker odd rows are
	bool interlace = false; ///< the new picture arrives in fields

	// the damaged cells (KinoGlitch's grid)
	int cellW = 1, cellH = 1, cols = 0, rows = 0;
	std::vector<uint8_t> cells; ///< per cell: 0 none, else bit 1 displaced, 2 the other picture, 4 shuffled, 8 crushed
	std::vector<uint8_t> cellRowOn; ///< whether a row of cells has any damage
	std::vector<int> cellDX, cellDY;

	struct Block {
		int x, y, w, h;    ///< where it's drawn
		int dx, dy;        ///< where from, relative
		int effect;        ///< 0 copy, 1 channels swapped, 2 inverted, 3 few colours
	};
	std::vector<Block> blocks;

	// the broken JPEG stream, in 8x8 blocks in reading order
	struct Corrupt {
		int b0, b1;        ///< the blocks it covers, [b0, b1)
		int shift;         ///< blocks the stream slides along
		int dr, dg, db;    ///< the colours' drift
		int u, v, amp;     ///< a frequency pattern (amp 0: none)
		bool lost;         ///< the blocks are lost (grey)
	};
	std::vector<Corrupt> corrupt;
	int jbw = 0, jbh = 0;

	struct SortBand {
		int y0, y1;        ///< rows [y0, y1)
		int lo, hi;        ///< the brightness range sorted
		bool desc;
	};
	std::vector<SortBand> sorts;

	int flash = 0;         ///< 0 none, 1 inverted, 2 one channel, 3 black and white, 4 outlines
	int flashChannel = 0;
};

/// The faults at `elapsed` ms of a transition of `time` ms (time 0: a still glitch at full
/// strength, at step `elapsed / step`)
Frame makeFrame(const Options &o, int width, int height, uint64_t elapsed, uint64_t time);

/// Whether the pixel at (x, y) shows the new picture at this moment (before the faults move it)
bool isNew(const Frame &f, int x, int y);

/// Draws the rectangle (left, top, w, h) of the frame into dest (at destLeft, destTop): from
/// `from` and `to` (the same picture for a still glitch; both the frame's size). Safe to call
/// for pieces of the frame at once, from several threads.
void draw(const Frame &f, const Picture &from, const Picture &to, int left, int top, int w, int h, uint8_t *dest,
          long destPitch, int destLeft, int destTop);

} // namespace glitch
