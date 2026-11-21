---
title: "GMD2 Lossless RGB8 Atlas Format"
subtitle: "Byte-Level Format Specification, Encoder/Decoder Instructions, and Tested C11 Example"
author: "Implementation reference for an Odin port"
date: "20 November 2026"
geometry: margin=0.8in
fontsize: 10pt
colorlinks: true
---

# 1. Status and design boundary

This describes the experimental GMD2 archive format used for the historical **101,626-byte** lossless Adelle Mono texture archive. The current checkout tests the C implementation with an openly licensed [JetBrains Mono fixture](../tests/fixtures/jetbrains-mono/).

The input is a **top-down, row-major RGB8 atlas** (three bytes per pixel). The codec does **not** encode alpha, glyph metrics, kerning, Unicode mappings, or vector outlines. The decoder requires the corresponding external `FontLayoutMsgp` layout so it can determine glyph rectangles and restore each pixel to its original atlas coordinate.

The archived reference input is a 504×504 atlas. The format stores generic width/height, but the sample C decoder is deliberately optimized for clarity, not unbounded atlas sizes or hostile input streams.

# 2. Files and building the C example

```
gmd2_final/
  Makefile
  include/               standalone encoder and decoder
  src/                   header template, metadata parsers, and archive CLI
  tests/
    fixtures/jetbrains-mono/
      atlas.gmd2
      layout.json
      atlas.rgb
    test_edge_cases.py
```

System requirements: C11 compiler, **libzstd** headers and library, and the C math library. The standalone CLI is designed for a Unix-like build environment.

```sh
make

fixture=tests/fixtures/jetbrains-mono
# Decode the bundled test vector and verify it
./gmd2tool decode \
  "$fixture/atlas.gmd2" \
  "$fixture/layout.json" \
  restored.rgb
cmp restored.rgb "$fixture/atlas.rgb"

# Build a new, valid archive and verify it
./gmd2tool encode "$fixture/atlas.rgb" "$fixture/layout.json" generated.gmd2 22
./gmd2tool decode generated.gmd2 "$fixture/layout.json" roundtrip.rgb
cmp roundtrip.rgb "$fixture/atlas.rgb"

# Full bundled check
make check
python3 tests/test_edge_cases.py
```

**Important:** The clear reference encoder does **not** perform the historical offline Zstd-guided exact-mode optimization. Its archive was **102,304 B**, while the optimized reference archive is **101,626 B**. Both reconstruct the same input perfectly. Decoder compatibility does **not** require identical compressed bytes.

# 3. Archive on-disk structure

All multibyte GMD2 header integers are **little-endian**. The single Zstd frame uses Zstd's own standard framing.

| Offset | Bytes | Field | Meaning |
|--:|--:|:--|:--|
| 0 | 4 | `magic` | ASCII `GMD2` |
| 4 | 4 | `width` | `uint32_le`, atlas width |
| 8 | 4 | `height` | `uint32_le`, atlas height |
| 12 | 1 | `channel_map_id` | 0–23, selection of a shared RGB channel per mask |
| 13 | 1 | `flags` | Must be **zero** in the current format |
| 14 | 1 | `shared_palette[0]` | Shared-value palette entry |
| 15 | 1 | `shared_palette[1]` | Shared-value palette entry |
| 16 | 1 | `exception_palette[0]` | Exceptional-value palette entry |
| 17 | 1 | `exception_palette[1]` | Exceptional-value palette entry |
| 18 | rest | `zstd_frame` | Exactly **one** Zstd frame containing the logical body |

The 101,626-byte Adelle archive has dimensions 504×504, map ID `18`, flags `0`, shared palette `{255,0}`, exceptional palette `{0,255}`. These particular palette values are **not universal constants**; read them from the header.

Decoded Zstd body:

```
body = symbols[N]
     || exceptional_residuals[NE]
     || shared_residuals[NS]
     || escapes[3 * NX]

N  = width * height
NE = number of exceptional fallback modes
NS = number of shared fallback modes
NX = number of all-distinct RGB pixels
```

`||` denotes byte concatenation. The symbols **must come first**, and exceptional residual bytes precede shared residual bytes. There are no lengths in the header for the latter three streams: compute them after decoding the `N` symbols. On the reference sample:

| Stream | Size before Zstd |
|:--|--:|
| Combined symbol bytes | 254,016 B |
| Exceptional residual bytes | 20,079 B |
| Shared residual bytes | 65,961 B |
| Escapes, 10 × RGB8 | 30 B |
| **Total** | **340,086 B** |

An upper bound for the uncompressed body is **4 × N bytes**, because an all-distinct texel has one symbol plus three RGB escape bytes; other texels have at most one symbol plus two one-byte residuals. The C decoder allocates this bound, then validates the exact body size.

# 4. Symbol byte: channel mask and two value modes

There is **one combined symbol byte per pixel**:

$$
\text{symbol}=\text{mask}+5\cdot\text{sharedMode}+30\cdot\text{exceptionMode}
$$

To unpack:

```c
int mask           = symbol % 5;
int shared_mode    = (symbol / 5) % 6;
int exception_mode = symbol / 30;
```

The mask encodes which channels agree:

| `mask` | Relation | Shared value | Exceptional channel |
|--:|:--|:--|:--|
| 0 | `R=G=B` | All channels | None |
| 1 | `G=B` | `G`/`B` | `R` |
| 2 | `R=B` | `R`/`B` | `G` |
| 3 | `R=G` | `R`/`G` | `B` |
| 4 | All distinct | None | Escape: store exact RGB |

For `mask=4`, both value modes must be zero and the next three escape bytes provide `R,G,B` directly. For `mask=0`, the exceptional mode must be zero because there is no exceptional value. For `mask=1..3`, both modes may be used. The decoder should reject mode values beyond 5 and forbidden combinations.

**Canonical mask classification for the encoder:**

```c
if      (r == g && g == b) mask = 0;
else if (g == b)           mask = 1;
else if (r == b)           mask = 2;
else if (r == g)           mask = 3;
else                       mask = 4;
```

# 5. Shared channel selection (header map ID)

Even when two channels agree, **which actual RGB channel** is used to read a neighboring pixel affects the spatial predictor. The map ID chooses a valid shared channel for each mask:

```c
shared_channel[0] = map_id / 8;                 // R, G or B
shared_channel[1] = 1 + (map_id % 8) / 4;       // G or B
shared_channel[2] = 2 * ((map_id % 4) / 2);     // R or B
shared_channel[3] = map_id % 2;                 // R or G
shared_channel[4] = 1;                          // unused for escape
```

Channels are numbered `R=0, G=1, B=2`. The reference map ID is **18**; its choices are: `mask0→B`, `mask1→G`, `mask2→B`, `mask3→R`. The *numerical shared value* is always the equal value; this choice only controls which already reconstructed RGB channel supplies predictor neighbors.

For exceptional pixels, the channel index is simply `mask-1`: mask 1 selects R, mask 2 selects G, and mask 3 selects B.

# 6. The four causal neighbor locations

The predictor reads previously reconstructed **full RGB** pixels, not previous raw residuals. For each target pixel, define:

- `L`: preceding pixel in the same traversal row.
- `U`: previous traversal row, same column.
- `UL`: previous traversal row, preceding column.
- `UU`: two traversal rows earlier, same column.

Unavailable neighbors are **zero** (per RGB channel). A neighbor is available only if its pixel has already been decoded. Never read from a future pixel or a different glyph region.

**Subtle but essential:** Glyph rectangles are serialized **bottom-to-top** in top-down image coordinates. Consequently, `U` is a *previous row in the GMD2 traversal*, which is physically **below** the current row in the output atlas for glyph pixels. Conversely, the outside-glyph region is serialized in ordinary top-down scanline order; there, `U` is physically above the current pixel. The C source explicitly creates causal neighbor indices for each case.

# 7. Integer prediction functions

The predictors use ordinary unbounded signed integer intermediates; applying `uint8_t` at the final step implements `mod 256` where appropriate.

### Shared value: Paeth

$$
p=a+b-c,\quad
\hat s=\begin{cases}
a & |p-a|\le|p-b|\ \text{and}\ |p-a|\le|p-c|\\
b & |p-b|\le|p-c|\\
c & \text{otherwise}
\end{cases}
$$

Here `a=L`, `b=U`, `c=UL`, all read from the **shared channel** selected by the map ID. Ties follow the ordering above: left wins first, above wins second, upper-left last.

```c
static uint8_t paeth(int a, int b, int c) {
    int p = a + b - c;
    int da = abs(p-a), db = abs(p-b), dc = abs(p-c);
    return (uint8_t)((da <= db && da <= dc) ? a :
                     (db <= dc) ? b : c);
}
```

### Exceptional value: gradient and vertical linear

Read the exceptional channel's `L`, `U`, `UL`, `UU` values.

$$
\text{gradient}=(L+U-UL)\bmod 256
$$

$$
\text{verticalLinear}=(2U-UU)\bmod 256
$$

The shared modes can use plain `L`/`U` or Paeth as exact values; the exceptional modes can use `L`, gradient, or vertical-linear. **No clipping** is applied to gradient or vertical-linear values: the modulo-256 rule is deliberate.

# 8. Shared and exceptional mode tables

## Shared-value mode (0–5)

| Mode | Decoder computes | Consumes residual? |
|--:|:--|:--|
| 0 | `shared_palette[0]` | No |
| 1 | `shared_palette[1]` | No |
| 2 | `Paeth(L,U,UL)` | No |
| 3 | `L` | No |
| 4 | `U` | No |
| 5 | `(Paeth(L,U,UL) + next_shared_residual) & 255` | **One byte** |

## Exceptional-value mode (0–5)

| Mode | Decoder computes | Consumes residual? |
|--:|:--|:--|
| 0 | `exception_palette[0]` | No |
| 1 | `exception_palette[1]` | No |
| 2 | `(L+U-UL) & 255` | No |
| 3 | `L` | No |
| 4 | `(2*U-UU) & 255` | No |
| 5 | `(L+U-UL + next_exception_residual) & 255` | **One byte** |

After decoding shared value `s`, initialize the current RGB pixel to `(s,s,s)`. If `mask` is 1–3, overwrite the exceptional channel with decoded `e`. This full reconstructed RGB pixel then becomes available to future predictions.

**Residuals are unsigned wrapped byte differences**, not signed ZigZag in GMD2. For an encoder value `v` and prediction `p`, store `(v-p)&255`. The decoder recovers `(p+delta)&255`; therefore `255→0` naturally corresponds to a residual of `+1`.

# 9. Reconstructing the metadata-driven traversal

Use `FontLayoutMsgp`, or the equivalent already parsed Odin layout, to obtain dimensions and each glyph's `Index` and `AtlasBounds`. The reference C parser reads these exact MessagePack keys:

```
layout.a.w        width
layout.a.h        height
layout.g[].i      glyph index
layout.g[].b.l    left
layout.g[].b.b    bottom
layout.g[].b.r    right
layout.g[].b.t    top
```

For each glyph:

```text
glyph_width  = round_nearest_even(right - left)
glyph_height = round_nearest_even(top - bottom)

if width <= 0 or height <= 0:
    skip this glyph rectangle

x = floor(left)
y = atlas_height - ceil(top)
```

This is specifically the rule used by the original supplied layout, with its `y="bottom"` convention. The C code handles the supplied half-pixel bounds. Use consistent IEEE rounding rules in an Odin port; do **not** silently replace nearest-even with truncation. Different atlas generators or origin conventions may require another format version or agreed conversion rule.

1. Validate that all nonempty rectangles are in bounds and **do not overlap**.
2. Sort nonempty glyphs by **width descending**, then **height ascending**, then **glyph Index ascending**. If all three keys tie, preserve their original metadata order.
3. Append each glyph's pixel coordinates in **reverse atlas-row order** (bottom row first), while keeping pixels left-to-right inside each row.
4. Mark every visited atlas texel as covered.
5. Append all **uncovered texels** in ordinary top-down atlas row-major order.

The decoder receives `N` encoded texels in this order and writes each reconstructed RGB triple to its original atlas coordinate. The current C implementation uses a cursor over the sorted rectangles and a one-bit-per-pixel coverage map.

### Neighbor construction

Inside a glyph, compute `L/U/UL/UU` **only within that glyph's local traversal rectangle**. For uncovered pixels, use the atlas offsets `(-1,0)`, `(0,-1)`, `(-1,-1)`, and `(0,-2)`. Each in-bounds offset points to an earlier raster position. That pixel was reconstructed during a glyph visit or earlier in the uncovered scan. Return zero for out-of-bounds neighbors.

# 10. Decoder pseudocode: exact stage order

```
validate header, dimensions and map ID
construct traversal and four causal neighbor maps
zstd_decompress_one_frame() -> body
N = width * height
symbols = body[0:N]

scan symbols to determine NE, NS, NX
require len(body) == N + NE + NS + 3*NX
exception_deltas = body[N:N+NE]
shared_deltas    = body[N+NE:N+NE+NS]
escape_RGB       = body[N+NE+NS:]

for i in range(N):
    s = symbols[i]
    mask = s % 5
    shared_mode = (s / 5) % 6
    exception_mode = s / 30

    if mask == 4:
        RGB[i] = next_three_escape_bytes()
        continue

    shared_channel = decode_channel_map(map_id, mask)
    a,b,c = causal_neighbors_RGB(i, shared_channel)
    base_prediction = paeth(a,b,c)
    shared = decode_shared_mode(shared_mode, base_prediction, a,b)
    RGB[i] = (shared, shared, shared)

    if mask != 0:
        exceptional_channel = mask - 1
        a,b,c,bb = causal_neighbors_RGB(i, exceptional_channel)
        exceptional = decode_exception_mode(exception_mode, a,b,c,bb)
        RGB[i][exceptional_channel] = exceptional

scatter_RGB_to_original_atlas_coordinates()
verify all delta/escape cursors consumed exactly
```

Processing shared first is important: the newly reconstructed pixel is fully initialized before it can be read by a later pixel.

# 11. Encoder steps: valid baseline versus optimized result

A correct encoder need not reproduce the same bitstream as the historical encoder. The following describes the straightforward C example:

1. Load the top-down RGB8 source and matching layout.
2. Construct the same traversal/neighbor mapping as the decoder.
3. Classify each ordered texel into mask 0–4.
4. Count shared/exceptional byte frequencies. Pick each stream's two most frequent values (tie-break by lower byte value); write the four palette bytes.
5. Choose shared map ID `18` for the sample/reference implementation. An advanced encoder may search all 24 valid map IDs and choose the best Zstd archive.
6. For each non-escape pixel, test the appropriate palettes first, then valid **exact** predictors; if none matches, select mode 5 and emit unsigned wrapped residual.
7. Serialize symbols first, exceptional residuals second, shared residuals third, raw RGB escape triples last.
8. Compress the body as one Zstd frame and prefix the 18-byte header.
9. **Always self-decode and compare the reconstructed RGB** to the source, including all uncovered pixels.

The historic 101,626-byte archive added an **offline mode-assignment search**. Whenever several exact predictors yield identical bytes, the offline encoder can change mode IDs and re-run Zstd, adopting a choice that improves compressed size. Palette ordering was also optimized. Neither requires extra decoding logic because the mode IDs are unchanged. The C encoder deliberately omits this expensive search for readability and emits a slightly larger, still compatible archive.

# 12. Essential C API and integration

Public header (complete declarations in `src/gmd2.h`):

```c
int gmd2_layout_from_msgpack(const uint8_t *msgp, size_t n,
                            Gmd2Metadata *layout,
                            char *error, size_t error_cap);

int gmd2_archive_decode(const uint8_t *archive, size_t archive_n,
                const Gmd2Metadata *layout,
                uint8_t **rgb_out, size_t *rgb_n,
                char *error, size_t error_cap);

int gmd2_archive_encode(const uint8_t *rgb, size_t rgb_n,
                const Gmd2Metadata *layout, int zstd_level,
                uint8_t **archive_out, size_t *archive_n,
                char *error, size_t error_cap);
```

On success, archive functions return output buffers allocated with `malloc()`. Release them with `free()`. Release glyph storage created by the metadata parser with `gmd2_free_layout()`. Error messages use caller-provided storage.

For a production Odin integration, it is better to reuse your **existing MessagePack parser** and pass glyph rectangles directly to the codec. Do not introduce a second MessagePack dependency just for compression. The C reader exists only to make the example independently executable.

# 13. Historical Adelle Mono test vectors

### Historical reference archive

```
Filename: adelle_reference_101626.gmd2
Length:   101626 bytes
Magic:    GMD2
Width:    504
Height:   504
Map ID:   18
Flags:    0
Palettes: shared=[255,0], exceptional=[0,255]
```

### Original RGB8 atlas

```
Length: 762048 bytes
SHA256: e433620a85e7f71e095d2986a40000a0227db623a8b02cd0eff425922dc00a1a
```

### C example encoder archive

```
Length (tested with installed libzstd, level 22): 102304 bytes
Decoded RGB8 SHA256: same as original
```

`make check` uses the bundled JetBrains Mono fixture to verify the reference decode and a fresh C encode/decode round trip against the original pixels. `python3 tests/test_edge_cases.py` checks 23 additional lossless input/layout cases and rejection of a bad format flag. The historical Adelle archive was also decoded in the original Python implementation.

# 14. Safety, limitations, and compatibility

- **Format version:** The magic `GMD2` and flags 0 identify this experimental implementation; do not change symbol behavior under the same signature.
- **Input trust:** Set application-specific maximum dimensions, number of glyphs, total allocations, and decompression time. The included code is a readable reference, not a security-audited parser.
- **Pixel format:** RGB8 only. It does not restore arbitrary RGBA arrays from `FontData`.
- **Layout dependence:** The exact atlas dimensions, glyph rectangles, origin, and sort ties must match the encoder. Metadata is neither self-contained nor optional.
- **One frame:** The decoder deliberately rejects appended data or concatenated Zstd frames. The smaller historical multi-frame experiments are **not** GMD2-compatible under this one-frame decoding contract.
- **Unsigned modulo arithmetic:** Ensure wraparound is byte-accurate in the port; use sufficiently wide signed intermediates for Paeth and gradient calculations.
- **Reconstruction order:** Do not parallelize individual texels without redesigning predictor dependencies.
- **Conformance:** Sample compilation and test coverage establish a baseline, not full formal verification.

# 15. Odin porting checklist

1. Decode MessagePack using the existing library; translate each glyph's bounds using the specified rounding and origin rules.
2. Build the deterministic nonempty-glyph order, uncovered-pixel list and causal neighbor lookups.
3. Read GMD2 header as little-endian; validate dimensions, `map_id < 24`, `flags == 0`.
4. Decompress exactly one Zstd frame, bounded by `4*pixel_count` bytes, and reject unexpected trailing frame data.
5. Validate symbols and derive stream sizes before consuming residuals.
6. Reconstruct RGB in traversal order; apply `u8` wrapping for residual addition and affine predictors.
7. Scatter ordered RGB back to atlas coordinates. Upload ordinary RGB8 texture (or expand to RGBA8 with a deliberately chosen alpha policy outside this codec).
8. Compare the resulting buffer to the supplied SHA-256 test vector.
9. Measure compiled decoder-only Odin code size, decode latency, transient allocation bytes, and final texture-plus-layout size on several fonts.

Verify byte-for-byte reconstruction using the bundled [JetBrains Mono fixture](../tests/fixtures/jetbrains-mono/) before optimizing memory or instruction count.
