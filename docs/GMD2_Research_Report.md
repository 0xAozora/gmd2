---
title: "GMD2: Lossless MSDF Atlas Compression"
subtitle: "Technical Research Report and Benchmark Retrospective"
author: "Engineering research summary"
date: "20 November 2026"
geometry: margin=0.85in
fontsize: 10pt
colorlinks: true
---

# Executive summary

This report records the original Adelle Mono research. The current checkout uses the openly licensed [JetBrains Mono fixture](../tests/fixtures/jetbrains-mono/) for reproducible tests.

**Recommendation:** Retain **GMD2 + a single Zstandard frame** as the current production-oriented candidate for the Adelle Mono RGB8 MSDF atlas. It gives **101,626 bytes** for the texture payload, compared with **153,309 bytes** for the original 38×38 block-planar + Zstd approach and **182,164 bytes** for untransformed RGB + Zstd. The format is lossless with respect to the supplied RGB8 bytes. It is experimental, not a public standard, and requires the original font layout metadata to decode.

The key insight is **MSDF-specific channel redundancy**. Of the 254,016 texels, 254,006 have at least two equal RGB channels. GMD2 encodes a shared distance and (when needed) an exceptional channel, then uses local channel-aware prediction and explicit modes to suppress predictable values. The resulting 340,086-byte transformed stream is compressed with an ordinary Zstd frame.

A standalone **C11 example encoder/decoder** accompanies this report. The C decoder has been compiled and used to decode the **actual 101,626-byte reference archive**, yielding an exact SHA-256 match to the supplied RGB file. The C encoder is a reference encoder rather than a reproduction of the offline optimized encoder: it generates **102,304 bytes** on the same atlas, and its result also round-trips exactly.

**Decision boundary:** We have no current evidence that the more complicated alternative transforms justify their implementation cost in Odin. The GMD2 decoder's principal requirements beyond Zstd are palette/mode decoding, small integer predictors, and a metadata-driven traversal and scattering operation. Actual compiled Odin size and decode speed have **not** yet been measured.

# 1. Scope and constraints

The goal evolved from reducing compressed MSDF texture size to finding a reasonable balance among:

1. Strict, byte-for-byte lossless reconstruction of the source **RGB8 atlas**.
2. Small compressed assets, principally with **Zstd**.
3. Small, maintainable runtime decoder, intended for **Odin**.
4. Complex or slow *offline encoding* is acceptable if the decoder stays simple.
5. The existing `FontLayoutMsgp` metadata is available at decode time; its cost must be accounted for separately when evaluating a complete font asset.

**Excluded:** perceptual / visually lossless quantization, alpha-channel reconstruction, and regeneration of MSDFs from font outlines. Earlier near-lossless candidates below 100,000 bytes were deliberately abandoned because byte-exact losslessness is required.

# 2. Dataset and measurement convention

| Item | Verified value |
|:--|--:|
| Font | Adelle Mono (as identified by supplied layout) |
| Texture format used in tests | RGB8, three bytes per pixel |
| Atlas dimensions | 504 × 504 |
| Pixel count | 254,016 |
| Uncompressed atlas | 762,048 B |
| MessagePack layout | 40,457 B |
| Glyph metadata entries | 524 |
| Nonempty glyph rectangles | 517 |
| Pixels in glyph rectangles | 222,805 (87.7%) |
| Pixels outside glyph rectangles | 31,211 (12.3%) |
| Zstd-compressed layout (separate) | 7,212 B |

**SHA-256 of the original RGB8 bytes:**

`e433620a85e7f71e095d2986a40000a0227db623a8b02cd0eff425922dc00a1a`

All reported **GMD2 archive sizes** refer to the compressed RGB texture only, **not** the metadata. For a complete font asset, a separately Zstd-compressed copy of the supplied MessagePack layout and the 101,626-byte texture total **108,838 bytes before additional container framing**. Compression ratios must not silently exclude metadata if comparing whole font packages.

The user's in-memory `FontData.Atlas` is described as RGBA. The tested raw upload, however, is **RGB8**. The GMD2 reference format and C example intentionally encode RGB8 only; any alpha channel must be handled separately, omitted only if provably reconstructible, or introduced in a new format version.

# 3. What we discovered in the pixels

The source atlas is especially favorable to a shared-channel representation:

| Channel relationship | Pixels | Share |
|:--|--:|--:|
| `R = G = B` | 61,921 | 24.38% |
| Exactly two channels equal | 192,085 | 75.62% |
| All three channels distinct | **10** | ~0.004% |

An ordinary RGB image codec sees three byte samples at every pixel. GMD2 instead sees mostly a **shared value** and a possible **exceptional value** plus its channel identity. Saturated 0/255 values and exact spatial predictions are also common.

The channel identity is **not an edge orientation**: MSDF RGB edge colors identify which colored distance functions participate around corners. We repeatedly found that choosing predictions using the **actual RGB channel** is more fruitful than treating all exceptional values as one undifferentiated scalar field.

# 4. Evolution of the lossless Zstd encoding

These are historical results from the previous benchmark rounds, not a newly rerun exhaustive optimization sweep. Stages frequently differ in multiple ways, so the improvements should not be interpreted as isolated causal effects without the individual ablation reports.

| Milestone | Compressed texture | Main development |
|:--|--:|:--|
| Raw interleaved RGB + Zstd | 182,164 B | Baseline general-purpose compression |
| Planar RGB + Zstd | 165,571 B | Separate channel storage |
| 38×38 block-planar + Zstd | 153,309 B | Original user baseline |
| MSDF shared-channel masks | 139,347 B | Specialized channel representation and palettes |
| Glyph-ordered masks | 117,945 B | Metadata-driven pixel traversal |
| Paeth / circular-delta family | 113,567 B | Residual prediction |
| Improved glyph ordering / exceptions | 112,518 B | Deterministic ordering and refined exceptions |
| Context split (GMC6) | 111,415 B | Contextual residual organization |
| Channel-aware RGB prediction (GMC8) | 106,611 B | Shared/exceptional predictors follow physical RGB channels |
| Exact-prediction tokens + outside-region prediction | 103,393 B | Omit residuals for exact hits; preserve atlas-space neighbors |
| Extra direction modes | 102,282 B | Vertical-linear exact prediction |
| Encoder-only symbol priorities / palettes, GMD2 | **101,626 B** | Offline mode assignment; same runtime decoder |

From the original 153,309-byte block-planar result to 101,626 bytes, the saving is **51,683 B (33.7%)**. Relative to the original uncompressed RGB atlas, the final archive is **86.7% smaller**, approximately **7.50:1**.

# 5. Current GMD2 layout at a glance

The deployed reference contains an 18-byte header and **one** Zstd frame:

```
+----------------------+  18 bytes
| GMD2 header/palettes |
+----------------------+
| Zstd frame           |
|   combined symbols   |  254,016 B before compression
|   exceptional deltas |   20,079 B
|   shared deltas      |   65,961 B
|   all-distinct RGB   |       30 B
+----------------------+
```

Thus the body passed to Zstd is **340,086 bytes**. The decoder derives stream lengths from the 254,016 symbols. It does not need to store per-glyph rectangles, mask run lengths, a separate LZ decoder, an FSE table, a block dictionary, or a learned predictor table inside GMD2.

For detailed byte-level rules and a tested C implementation, see **`GMD2_Format_and_C_Implementation.md`** and the `src/` directory in the delivery bundle.

# 6. Why GMD2 compresses well

**MSDF correlation:** A five-way class indicates whether all channels agree, which one is exceptional, or whether all three are distinct. Two frequent-byte palettes are used for shared and exceptional values.

**Causal integer predictors:** The shared value can match Paeth, left or a previous row; the exceptional channel can match a gradient, left or second-order vertical prediction. Any mismatch is stored as an **unsigned modulo-256 residual**, so the transform is exactly invertible. Pixels with an exact prediction need no residual byte at all.

**Glyph-local traversal:** Nonempty glyph rectangles are sorted by descending width, then ascending height, then glyph index. Each glyph is traversed from its last atlas row to its first. Pixels outside glyph bounds follow, in original scanline order. The decoder computes the same traversal from the unmodified metadata and scatters pixels to their original atlas locations.

**Offline symbol optimization:** Where several modes reconstruct the same value, the encoder may choose among them using actual Zstd compressed-size feedback. This changes the encoded symbols, not what the decoder understands. The 101,626-byte reference was obtained with this additional offline optimization; the simple portable C encoder does not reproduce that exact archive size.

# 7. Representative alternative codecs and transforms

A subset of the broader comparison is given below. These sizes are measurements for particular available encoders/settings on **this one atlas**, not universal rankings or guaranteed encoder optima.

| Comparison format | Texture bytes | Observed tradeoff |
|:--|--:|:--|
| GMD2 + Zstd −22 | **101,626** | Best small-decoder candidate |
| Raw RGB + Zstd −22 | 182,164 | Very simple decode, much larger |
| 38×38 block-planar + Zstd −22 | 153,309 | Trivial deinterleave, 51.7 KB larger |
| Optimized PNG | 240,090 | Standard interoperable format; significantly larger |
| Lossless WebP | 182,354 | Standard codec; no MSDF-aware correlation model |
| Lossless JPEG XL (tested Modular effort 9) | 213,402 | Modern lossless image coding, larger for tested configuration |
| BWT + MTF + standalone FSE, on GMD2 streams | 126,011 | More memory, more decode stages, still larger |
| 2D spatial LZ + Zstd, on GMD2 streams | 102,435 | Explicit copy instructions do not beat Zstd matching |

For reference only, **Brotli Q11** on the same GMD2 transform was tested at **98,334 B**, but the current requested implementation uses **Zstd**. Changing the entropy compressor does not improve the underlying MSDF representation and introduces a different runtime dependency.

Other approaches screened included: scalar RGB subtract-green transforms, explicit mask RLE and row-span boundary coding, normalized 2D block dictionaries, affine-region models, multiresolution lifting, symmetry prediction, JPEG-LS MED and gradient contexts, learned causal predictors, BWT, glyph references, RGB-copy instructions, local channel permutations, residual bit-plane transforms, and several Zstd parameter sweeps. No tested, completed alternative justified replacing GMD2 on compressed size **and** decoder simplicity.

# 8. Interpreting failed experiments correctly

The failed tests constrain the research direction, but do not prove a compression lower bound.

- **A lower numeric residual is not necessarily a lower Zstd cost.** Symbol and byte-sequence regularity mattered more than per-pixel prediction error.
- **Splitting combined mode symbols often hurt.** Separating channel identity and predictor-mode streams destroyed useful context that Zstd could match directly.
- **Normalized block matches were often redundant.** Many exact or normalized matches came from constant/saturated regions already cheap in the original representation; template references also have cost.
- **Geometric modeling is not the same as geometry-aware reproduction.** Raster-inferred planes or threshold-based distance transforms cannot recover original vector/MSDF color behavior without additional correction data.
- **BWT helped standalone FSE but not Zstd.** Reordering breaks the original LZ-friendly sequences; inverse BWT also needs substantial scratch space.
- **The outside region is not trivially removable.** Its 31,211 pixels were required for byte-for-byte equality, whether or not a particular renderer samples them.

# 9. Decoder engineering assessment

GMD2's operations beyond Zstd are comparatively basic. The C example contains no custom LZ, custom entropy coding, transforms with floating-point geometry, distance-transform reconstruction, or external model dictionary. Its essential operations are masks, palettes, Paeth, gradients, modulo-256 addition, and scatter writes.

The more important implementation concerns are:

- **Metadata dependence:** Decode the exact glyph positions and recreate the deterministic sort. A different rounding convention or coordinate origin will corrupt reconstruction.
- **Dependency graph:** Neighbor reads must come from **already reconstructed RGB pixels**. Glyph rows are locally reversed; the outside region uses actual atlas coordinates and a causal-availability check.
- **Input validation:** Verify compressed frame boundaries, header flags, glyph overlap/bounds, mode validity, residual counts, and buffer lengths. Untrusted fonts should also have application-defined maximum dimensions.
- **Memory:** This readable C version favors explicit `order`, inverse-order, and neighbor arrays. Its memory usage is not a lower bound for a carefully optimized Odin decoder; several neighbor indices can instead be computed on demand.
- **Deployability:** We have **not** benchmarked final Odin `.text` size, decode speed, or peak memory. Nor has this reference implementation undergone a security audit.

In the test environment, the **whole dynamically linked C command-line tool** (encoder, decoder, MessagePack helper, CLI; excluding the dynamically linked Zstd library's code) has approximately **18.9 KB of `.text`** at GCC `-O2`. This is **not** the code size of a decoder-only Odin implementation and must not be presented as such.

# 10. Reproducibility and verification

The attached source includes a minimal MessagePack reader for the required layout fields, a real GMD2 decoder, an encoder, and command-line utilities. No Python is required to execute the C codec; Python is used by the optional randomized tests.

```sh
make
make check
python3 tests/test_edge_cases.py
```

Historical validation on Adelle Mono:

- Decode the **historical 101,626-byte GMD2 archive** directly with the independent C decoder.
- Compare reconstructed RGB8 bytes with `cmp` and SHA-256 against the supplied input.
- Encode the original RGB file using the C example encoder at Zstd level 22: **102,304 bytes**.
- Decode that C-created archive and verify exact equality.
- Run **23 additional randomized/edge-case layouts and RGB patterns**, plus malformed-header rejection.

These tests establish interoperability for the sample and covered cases; they are not a substitute for an exhaustive specification-conformance suite or fuzzing.

# 11. Recommendation and next steps

1. **Freeze GMD2's existing 18-byte header, symbol modes and one-frame Zstd payload for an Odin prototype.** Do not add BWT, FSE, geometric regions or another match finder without a demonstrated total-size win.
2. **Port the C decode logic**, but use the existing Odin MessagePack layout parser instead of the included minimal C parser. Maintain exact traversal/rounding and neighbor semantics.
3. **Measure total bytes and runtime**, separating the compressed atlas, compressed layout, and decoder code. Benchmark several fonts and atlas resolutions before deciding on a stable production version.
4. Keep the encoder offline and optionally expensive. A simple reference encoder can be used for correctness; a separate optimizer can pursue additional archive-size savings without changing the decoder.
5. If an entire new representation is explored later, test actual complete archives and round-trip reconstruction, not only residual entropy or the number of exact predictions.

**Bottom line:** GMD2 is not literally PNG with Deflate replaced by Zstd. It is a metadata-aware, MSDF-specific reversible channel and prediction transform **followed by** Zstd. Within the tested alternatives, it remains the most attractive balance of storage savings, decode simplicity and accuracy for the supplied Adelle atlas.
