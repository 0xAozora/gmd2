package gmd2_encoder

import "core:c"
import types "../types"

when ODIN_ARCH == .wasm32 || ODIN_ARCH == .wasm64p32 {
    foreign import codec "gmd2_wasm.o"
} else {
    foreign import codec "libgmd2.a"
}

Bounds :: types.Bounds
Glyph :: types.Glyph
Artery_Advance :: types.Artery_Advance
Artery_Glyph :: types.Artery_Glyph
Y_Origin :: types.Y_Origin
Status :: types.Status
Atlas_View :: types.Atlas_View
Artery_Atlas_View :: types.Artery_Atlas_View

// Count glyphs in elements. The caller owns all buffers through the call.
@(default_calling_convention="c")
foreign codec {
    @(link_name="gmd2_encode_bound")
    encode_bound :: proc(width, height: u32, channels: c.uint) -> u32 ---

    @(link_name="gmd2_encode_scratch_size")
    encode_scratch_size :: proc(width, height: u32, glyph_count: u32) -> u32 ---

    @(link_name="gmd2_encode")
    encode :: proc(pixels: [^]u8, length: u32, atlas: ^Atlas_View, channels: c.uint,
        out: [^]u8, capacity: u32, scratch: rawptr, scratch_len: u32) -> i32 ---

    @(link_name="gmd2_encode_artery")
    encode_artery :: proc(pixels: [^]u8, length: u32, atlas: ^Artery_Atlas_View, channels: c.uint,
        out: [^]u8, capacity: u32, scratch: rawptr, scratch_len: u32) -> i32 ---
}
