package gmd2_decoder

import "core:c"
import types "../types"

foreign import codec "libgmd2.a"

Bounds :: types.Bounds
Glyph :: types.Glyph
Artery_Advance :: types.Artery_Advance
Artery_Glyph :: types.Artery_Glyph
Y_Origin :: types.Y_Origin
Status :: types.Status
Atlas_View :: types.Atlas_View
Artery_Atlas_View :: types.Artery_Atlas_View

// Count glyphs in elements. The caller owns all buffers through the call.
foreign codec {
    @(link_name="gmd2_decode_size")
    decode_size :: proc(data: [^]u8, length: c.size_t, channels: c.uint) -> c.size_t ---

    @(link_name="gmd2_decode_scratch_size")
    decode_scratch_size :: proc(width, height: u32, glyph_count: c.size_t) -> c.size_t ---

    @(link_name="gmd2_decode")
    decode :: proc(data: [^]u8, length: c.size_t, atlas: ^Atlas_View, channels: c.uint,
        out: [^]u8, capacity: c.size_t, scratch: rawptr, scratch_len: c.size_t) -> Status ---

    @(link_name="gmd2_decode_artery")
    decode_artery :: proc(data: [^]u8, length: c.size_t, atlas: ^Artery_Atlas_View, channels: c.uint,
        out: [^]u8, capacity: c.size_t, scratch: rawptr, scratch_len: c.size_t) -> Status ---
}
