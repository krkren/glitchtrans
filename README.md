# glitchtrans

A transition plugin for the Kirikiri Z engine (krkrz_dev): the `glitch` transition, the old picture
breaking up as a damaged video stream or a bad signal would, and coming back as the new one, and
a still glitch that scripts can draw over any layer for a moment of a "broken screen".

Written for [KAGEX](https://github.com/krkren/kagex), whose engines have it built in, but it
works with any game on those engines.

## For scripts

```
Plugins.link("glitchtrans.dll");
```

```
[backlay]
[image layer=base page=back storage=bg02]
[trans method=glitch time=1400]
[wt]

; the old picture melting into the new one
[trans method=glitch time=1400 style=mosh]

; the same scene, glitched for a moment
[backlay]
[trans method=glitch time=300 strength=70]
[wt]
```

```
GlitchTrans.apply(layer, %[seed: 3, strength: 80, style: "digital"]);   // one glitched frame, drawn over the layer
```

## What it draws

A glitch is made of the faults a broken digital stream or analog signal shows, layered and staged
over the transition's time. Its strength rises from nothing, holds through the middle and falls to
nothing at the end, with a few hiccups on either side and surges from step to step, so the first
and last frames are the pictures as they are. The faults jump, as a broken signal does: they change
at steps of irregular length (about `step` ms, some held much longer), and the slower ones take
turns, one leading for a while and the others joining now and then. The datamosh moves smoothly,
as it would on screen. The new picture arrives in macroblocks: in patches, in stripes or sweeping
across.

- **mosh**: a datamosh. The new picture's first frame is lost, so its motion is applied to the old
  picture: the old picture's macroblocks slide along a motion field (followed back step by step,
  so a block stops where the field stands still, and the content there repeats) or smear their
  own pixels along it, the new picture's edges come through on the old one's colours, and each
  macroblock is refreshed into the new picture at its own moment, some a little out of place
  before they snap in
- **jpeg**: a broken JPEG stream. From a point of the picture on, in 8x8 blocks in reading order,
  the colours drift (a JPEG codes each block's average as a difference from the last one, so one
  bad value tints everything after it), the blocks slide along by a few, a frequency pattern shows
  in some and a stretch of blocks may be lost
- **damage**: a grid of cells damaged in runs, as a decoder loses a stretch of its data: a cell
  shows another part of the picture, the other picture, its colours shuffled, or its colours
  crushed to a few with a dither
- **sort**: pixel sorting. In a few bands, runs of pixels whose brightness is in a range are sorted
  by brightness
- **analog**: a bad analog signal. Rows jitter, the picture shakes, rolls (with the black bar of
  the vertical blanking) or slants (horizontal hold), a tracking band with white streaks moves
  down, the colour lags behind the brightness and drifts, odd scan lines darken, and the new
  picture arrives in interlaced fields
- **bands**: the picture cut into horizontal bands, shifted sideways, stretched from a point or
  smeared down from their first row
- **split**: the red and blue channels apart from the green
- **noise**: rows of static, and in the digital styles rows of "memory garbage", bars of colour
- **blocks**: rectangles copied from elsewhere, some with their channels swapped, inverted or
  crushed
- **flash**: single frames of signal failure at the height: inverted, one channel, a hard black
  and white, or the new picture's outlines lit up

`style` says which faults lead; each fault's own option overrides the style's amount:

| `style` | bands | noise | blocks | damage | jpeg | mosh | sort | analog | flash |
|---|---|---|---|---|---|---|---|---|---|
| `mixed` (default) | 14 | 15 | 8 | 30 | 45 | 70 | 30 | 25 | 25 |
| `digital` | 18 | 25 | 12 | 70 | 75 | 0 | 45 | 0 | 35 |
| `analog` | 10 | 35 | 0 | 0 | 0 | 0 | 0 | 100 | 15 |
| `mosh` | 3 | 0 | 2 | 0 | 25 | 100 | 0 | 0 | 0 |

| Option | | Default |
|---|---|---|
| `time` | ms (the transition's) | |
| `style` | `mixed`, `digital`, `analog` or `mosh` (`datamosh`) | `mixed` |
| `seed` | a number: the same seed draws the same glitch | a new one each time |
| `strength` | 0-100, how strong at its height | 100 |
| `bands` | how many bands | the style's |
| `shift` | the most a band moves, px | a twelfth of the width |
| `split` | the most the colours move apart, px | width / 96 |
| `noise` | 0-100, rows of static and garbage | the style's |
| `blocks` | displaced rectangles at the height | the style's |
| `damage`, `jpeg`, `mosh`, `sort`, `analog`, `flash` | 0-100, each fault's amount | the style's |
| `step` | about how many ms a jump holds | 50 |
| `spread` | 0-100, how far apart (of the whole time) the parts of the new picture arrive | 60 |

`GlitchTrans.apply(layer, options)` takes the same options as a dictionary (no `time`; `frame`
picks which jump to draw) and draws the height of a glitch over the layer's own picture; false
when the layer has none.

The transition is an exchange transition, as `crossfade` is, and both pictures must be the same
size. A piece of the picture moved by a fault takes its alpha with it, so a layer that glitches in
or out on its own (a character, a CG over the scene) breaks up with its shape, its pieces showing
over what's behind it; on a whole screen the pictures are opaque anyway. It draws on the engine's
draw threads (`-drawthread`), bands of
rows at once.

## References

The techniques follow public glitch effects whose licences allow it, all MIT; no code is taken
from them, they're reimplemented here:

- [KinoGlitch](https://github.com/keijiro/KinoGlitch) by Keijiro Takahashi: the damaged cells
  (runs over a grid, KinoGlitch's thresholds and its channel shuffle), the rows' jitter and the
  colour drift
- [gl-transitions](https://github.com/gl-transitions/gl-transitions)' GlitchMemories (Gunnar Roth)
  and GlitchDisplace (Matt DesLauriers): channels offset by block, and a flash of the outlines
- [glitch-canvas](https://github.com/snorpey/glitch-canvas) by Georg Fischer: corrupting a JPEG's
  scan data, whose look the JPEG faults draw directly

The datamosh and the JPEG faults follow how video and JPEG decoders fail (motion applied to the
wrong picture, differentially coded DC values).

## Building

As part of the engine: add this repository's `plugins` folder to krkrz_dev's
`TVP_PLUGIN_FOLDERS` (after krkrz_dev's own `src/plugins`, whose tp_stub and ncbind it uses) and
`glitchtrans` to `TVP_PLUGINS`, or to a platform kit's `pluginFolders` and `plugins`.

On its own, with its tests:

```
cmake -B build -DKRKRZ_DEV_DIR=/path/to/krkrz_dev && cmake --build build && ctest --test-dir build
```

`glitch.cpp` has no engine in it; `tests/glitch_test.cpp` checks, for every style, that the first
and last frames are the pictures as they are, that a seed and a moment always draw the same
picture (whole or in the engine's pieces), that the new picture has arrived everywhere by the end,
that strength 0 is a cut, that the alpha goes with the pieces (a layer coming in from nothing shows
pieces of itself early); that the jumping faults hold within a step
and jump at the next while a datamosh moves on, that the styles differ and that each fault can be
turned off.

## Licence

0BSD (`LICENSE`).
