# GMD2

GMD2 is a lossless codec for RGB8 MSDF and RGBA8 MTSDF atlases. It ships as two standalone C headers with Odin, Go, and Rust bindings. The core emits raw GMD2 bytes that you can store directly or compress separately.

## Generate a font atlas

Generate an atlas from a `.ttf` or `.otf` font with the `msdf-atlas-gen` command-line tool. [msdf-atlas-gen repository](https://github.com/Chlumsky/msdf-atlas-gen)

```sh
msdf-atlas-gen -font font.ttf -type msdf -size 32 -pxrange 4 \
  -yorigin top -format bin -imageout atlas.rgb -json layout.json
```

This writes top-down RGB8 pixels to `atlas.rgb` and the glyph layout to `layout.json`. For RGBA8 MTSDF, use `-type mtsdf` and `-imageout atlas.rgba`, then pass the pixels to the C API or a binding.

## C API

Copy [gmd2_encoder.h](include/gmd2_encoder.h), [gmd2_decoder.h](include/gmd2_decoder.h), or both. For each header you use, define its implementation macro (`GMD2_ENCODER_IMPLEMENTATION` or `GMD2_DECODER_IMPLEMENTATION`) in one translation unit before including it.

| Glyph records | Atlas view | Encoder | Decoder |
|---|---|---|---|
| Parsed msdf-atlas-gen JSON, `Gmd2Glyph` | `Gmd2AtlasView` with `y_origin` | `gmd2_encode` | `gmd2_decode` |
| Parsed Artery Font, `Gmd2ArteryGlyph` | `Gmd2ArteryAtlasView` with `image_index` | `gmd2_encode_artery` | `gmd2_decode_artery` |

Pass tightly packed, top-down pixels and use the same glyph layout for encoding and decoding. Set `y_origin` from the JSON metadata. Artery bounds use a bottom origin. The view borrows the glyph array for the call. For glyph-aware streams, the codec uses the atlas dimensions, glyph indices, atlas bounds, and coordinate origin. Keep the glyphs in the same order at both ends.

To encode an atlas in row-major order without glyph metadata, pass `NULL` and a glyph count of zero to both calls. Supply the atlas dimensions in either case.

```c
Gmd2AtlasView atlas = {
    .width = width,
    .height = height,
    .glyphs = glyphs,
    .glyph_count = glyph_count,
    .y_origin = GMD2_ORIGIN_TOP,
};
uint32_t capacity = gmd2_encode_bound(atlas.width, atlas.height, 0);
uint32_t scratch_len = gmd2_encode_scratch_size(atlas.width, atlas.height, atlas.glyph_count);

/* Supply encoded and scratch buffers with the capacities above. */
int32_t written = gmd2_encode(pixels, pixel_len, &atlas, 0, encoded, capacity,
                              scratch, scratch_len);
```

Lengths, capacities, glyph counts, and size queries use 32-bit unsigned integers. On success, `written` is the positive stream length. The encoder requires its worst-case bound to fit in `INT32_MAX` bytes. In Wasm, the result is a JavaScript number. For decoding, `gmd2_decode_size(stream, length, channels)` reads the header to give the output size. Use `gmd2_decode_scratch_size(width, height, glyph_count)` for scratch. Size queries return zero on error. The codec performs no heap allocations. The caller supplies disjoint buffers, and scratch can be reused between calls at any alignment.

Use `3` for RGB8, `4` for RGBA8, or `0` for automatic selection. With `0`, the encoder infers channels from the exact input byte count and `gmd2_encode_bound` reserves space for either format. The decoder selects RGBA8 when the stream stores alpha, and RGB8 otherwise. Set `4` explicitly to fill omitted alpha with `255`.

For stored alpha, the encoder writes `alpha - median(R, G, B)` modulo 256 in the same pixel order as the mode bytes. This sets the median-alpha flag (`0x04`) and requires an updated decoder. The updated decoder also reads existing streams with raw alpha.

Decode returns `GMD2_OK` (0) on success. Both functions return a negative status on error.

| Status | Meaning |
|---|---|
| `GMD2_ERR_INVALID_ARGUMENT` | A required pointer, channel count, or pixel byte count is invalid. |
| `GMD2_ERR_INVALID_LAYOUT` | The atlas dimensions or glyph data are invalid or disagree with the stream. |
| `GMD2_ERR_INVALID_STREAM` | The raw GMD2 stream is malformed, truncated, or unsupported. |
| `GMD2_ERR_OUTPUT_TOO_SMALL` | Increase the output buffer capacity. |
| `GMD2_ERR_SCRATCH_TOO_SMALL` | Supply scratch sized by the scratch query. |
| `GMD2_ERR_CHANNEL_MISMATCH` | The stream stores alpha. Request RGBA8 or automatic output. |

## Language bindings

| Language | Encoder only | Decoder only |
|---|---|---|
| Odin | Run `make odin-encoder` and import `bindings/odin/encoder` | Run `make odin-decoder` and import `bindings/odin/decoder` |
| Go | Import `gmd2/encoder` | Import `gmd2/decoder` |
| Rust | Enable `encoder` with default features disabled | Enable `decoder` with default features disabled |

For both codecs, run `make odin` and import `bindings/odin`, import both Go packages, or use the default Rust features. Go uses cgo, and Rust exposes unsafe FFI functions.

In Odin, you can build the view from your existing slice. Cast the glyph pointer when your own record matches `gmd2.Glyph` in layout, and keep the slice alive through the call.

```odin
atlas := gmd2.Atlas_View{
    Width = font.AtlasData.Width,
    Height = font.AtlasData.Height,
    Glyphs = cast([^]gmd2.Glyph)raw_data(font.Glyphs),
    Glyph_Count = u32(len(font.Glyphs)),
    Y_Origin = .Bottom, // Match the metadata.
}
```

## WebAssembly

For Odin, run `make odin-wasm` to build the freestanding objects, or use
`make odin-encoder-wasm` or `make odin-decoder-wasm` for one codec. The
bindings link these objects on `wasm32` and `wasm64p32` and use Odin's
runtime memory routines. Use `make odin` for native builds.

Build a standalone decoder module with Zig.

```sh
zig cc -target wasm32-wasi -Oz -c bindings/decoder.c -o gmd2_decoder.o
zig cc -target wasm32-wasi -nostdlib -Wl,--no-entry -Wl,--strip-all \
  -Wl,-z,stack-size=65536 -Wl,--export=__heap_base \
  -Wl,--export=gmd2_decode -Wl,--export=gmd2_decode_size \
  -Wl,--export=gmd2_decode_scratch_size gmd2_decoder.o -o gmd2_decoder.wasm
```

This build has zero host imports with Zig 0.16.0. Grow the exported `memory` as needed, then place buffers and the atlas view in disjoint regions at or above `__heap_base`. Pass their byte offsets as pointers. The wasm32 atlas view is five little-endian 32-bit fields in order of width, height, glyph pointer, glyph count, and Y origin. For Artery, export `gmd2_decode_artery` and use the image index in the last field.

## Archive CLI

The optional CLI compresses the RGB8 GMD2 body with Zstd and takes a separate msdf-atlas-gen JSON or `FontLayoutMsgp` MessagePack layout. Build it with a C11 compiler, libzstd, and libm.

```sh
make gmd2tool
fixture=tests/fixtures/jetbrains-mono
./gmd2tool encode "$fixture/atlas.rgb" "$fixture/layout.json" atlas.gmd2 22
./gmd2tool decode atlas.gmd2 "$fixture/layout.json" restored.rgb
cmp "$fixture/atlas.rgb" restored.rgb
```

| JetBrains Mono RGB8 atlas, 236 × 236 | Bytes |
|---|---:|
| Original pixels | 167,088 |
| Raw GMD2 | 78,011 |
| Archive CLI with Zstd level 22 | 30,324 |
| JSON layout, supplied separately | 21,018 |
| MessagePack layout | 7,042 |
| MessagePack layout with Zstd level 22 | 1,508 |
| GMD2 archive + Zstd-compressed MessagePack layout | 31,832 |

The MessagePack benchmark uses the compact `FontLayoutMsgp` keys and 32-bit floats. Decompress the layout before passing it to the CLI.

The JetBrains Mono fixture includes the SIL Open Font License 1.1 and its generation command. [Fixture provenance and license](tests/fixtures/jetbrains-mono/)

## Development

Run `make headers` after editing [src/gmd2_codec.in](src/gmd2_codec.in). When packaging both codecs into one static archive, compile `bindings/encoder.c` and `bindings/decoder.c` as separate objects so the linker can select the one the caller uses.

The Python tests require `msgpack`.

```sh
make check
python3 tests/test_edge_cases.py
make odin
odin test bindings/odin
(cd bindings/go && go test ./...)
cargo test --manifest-path bindings/rust/Cargo.toml
```

## Format and research

- Historical RGB8 Zstd archive format and C implementation notes. [Markdown](docs/GMD2_Format_and_C_Implementation.md) · [PDF](docs/GMD2_Format_and_C_Implementation.pdf)
- Adelle Mono research and benchmark results. [Markdown](docs/GMD2_Research_Report.md) · [PDF](docs/GMD2_Research_Report.pdf)

## License

- GPL 2.0
- A commercial license is available. Contact us at aozora [AT] reshade.io for more information.
