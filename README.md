# glitchtrans

A transition plugin for the Kirikiri Z engine (krkrz_dev): the `glitch` transition, the old picture
breaking up like a damaged digital signal and coming back as the new one, and a still glitch
that scripts can draw over any layer for a moment of a "broken screen".

Written for [KAGEX](https://github.com/krkren/kagex), whose engines have it built in, but it
works with any game on those engines.

## For scripts

```
Plugins.link("glitchtrans.dll");
```

```
[backlay]
[image layer=base page=back storage=bg02]
[trans method=glitch time=800]
[wt]

; the same scene, glitched for a moment
[backlay]
[trans method=glitch time=300 strength=70]
[wt]
```

```
GlitchTrans.apply(layer, %[seed: 3, strength: 80]);   // one glitched frame, drawn over the layer
```

The glitch changes in jumps, as a broken signal does: every `step` ms a new set of faults is
drawn. Its strength rises from nothing at the start to its height in the middle and falls to
nothing at the end, so the first and last frames are the pictures as they are, and the new
picture arrives in stripes, each at its own moment around the middle. The faults:

- **bands**: the picture cut into horizontal bands, each shifted sideways (the picture wraps
  round), some not at all
- **split**: the red and blue channels apart from the green
- **noise**: rows of static
- **blocks**: rectangles copied from elsewhere, some with their channels swapped, inverted or
  reduced to a few colours

| Option | | Default |
|---|---|---|
| `time` | ms (the transition's) | |
| `seed` | a number: the same seed draws the same glitch | a new one each time |
| `strength` | 0-100, how strong at its height | 100 |
| `bands` | how many bands | 18 |
| `shift` | the most a band moves, px | a twelfth of the width |
| `split` | the most the colours move apart, px | width / 96 |
| `noise` | 0-100, how many rows of static | 30 |
| `blocks` | displaced rectangles at the height | 12 |
| `step` | ms between the jumps | 50 |
| `spread` | 0-100, how far apart (of the whole time) the stripes of the new picture arrive | 60 |

`GlitchTrans.apply(layer, options)` takes the same options as a dictionary (no `time`; `frame`
picks which jump to draw) and draws the height of a glitch over the layer's own picture; false
when the layer has none.

The transition is an exchange transition, as `crossfade` is, and both pictures must be the same
size. It keeps each pixel's alpha.

## Building

As part of the engine: add this repository's `plugins` folder to krkrz_dev's
`TVP_PLUGIN_FOLDERS` (after krkrz_dev's own `src/plugins`, whose tp_stub and ncbind it uses) and
`glitchtrans` to `TVP_PLUGINS`, or to a platform kit's `pluginFolders` and `plugins`.

On its own, with its tests:

```
cmake -B build -DKRKRZ_DEV_DIR=/path/to/krkrz_dev && cmake --build build && ctest --test-dir build
```

`glitch.cpp` has no engine in it; `tests/glitch_test.cpp` checks that the first and last frames
are the pictures as they are, that a seed and a moment always draw the same picture (whole or
in the engine's pieces), that the glitch jumps between steps and holds within one, that every
stripe of the new picture has arrived by the end and that the alpha is kept.

## Licence

0BSD (`LICENSE`).
