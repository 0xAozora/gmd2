package gmd2

import "core:bytes"
import "core:c"
import "core:testing"

// These are the caller's own types, distinct from the binding declarations.
Font_Bounds :: struct { Left, Bottom, Right, Top: f32 }
Font_Glyph :: struct {
	Index: u32,
	Advance: f32,
	PlaneBounds, AtlasBounds: Font_Bounds,
}

#assert(size_of(Glyph) == size_of(Font_Glyph))
#assert(offset_of(Glyph, AtlasBounds) == offset_of(Font_Glyph, AtlasBounds))
#assert(size_of(Artery_Glyph) == 48)

@(test)
fixtures :: proc(t: ^testing.T) {
	json := Font_Glyph{Index = 7, Advance = 0.75, AtlasBounds = {0.5, 0, 1.5, 2}}
	top := json
	top.AtlasBounds.Top, top.AtlasBounds.Bottom = 0, 2
	artery := [?]Artery_Glyph{{Image = 1}, {Codepoint = 7, ImageBounds = {0.5, 0, 1.5, 2}}}
	bottom_atlas := Atlas_View{Width = 2, Height = 2, Glyphs = cast(^Glyph)&json, Glyph_Count = 1, Y_Origin = .Bottom}
	top_atlas := Atlas_View{Width = 2, Height = 2, Glyphs = cast(^Glyph)&top, Glyph_Count = 1, Y_Origin = .Top}
	artery_atlas := Artery_Atlas_View{Width = 2, Height = 2, Glyphs = &artery[0], Glyph_Count = 2, Image_Index = 0}
	expected_streams := [?][]u8{
		#load("../../tests/fixtures/rgb8.gmd2"),
		#load("../../tests/fixtures/rgba8.gmd2"),
	}
	for expected, i in expected_streams {
		channels := c.uint(3 + i)
		pixels := make([]u8, 4 * channels)
		defer delete(pixels)
		for &pixel, j in pixels { pixel = u8(j * 13) }
		encoded := make([]u8, encode_bound(2, 2, 0))
		defer delete(encoded)
		decoded := make([]u8, decode_size(raw_data(expected), c.size_t(len(expected)), 0))
		defer delete(decoded)
		scratch_size := encode_scratch_size(2, 2, 2)
		testing.expect(t, scratch_size == decode_scratch_size(2, 2, 2))
		scratch := make([]u8, scratch_size)
		defer delete(scratch)
		pixel_count, stream_count := c.size_t(len(pixels)), c.size_t(len(expected))
		capacity := c.size_t(len(encoded))

		written := encode(raw_data(pixels), pixel_count, &bottom_atlas, 0,
			raw_data(encoded), capacity, raw_data(scratch), scratch_size)
		testing.expect(t, written == i32(len(expected)) && bytes.equal(encoded[:written], expected))
		written = encode_artery(raw_data(pixels), pixel_count, &artery_atlas, channels,
			raw_data(encoded), capacity, raw_data(scratch), scratch_size)
		testing.expect(t, written == i32(len(expected)) && bytes.equal(encoded[:written], expected))

		testing.expect(t, decode(raw_data(expected), stream_count, &top_atlas, 0,
			raw_data(decoded), pixel_count, raw_data(scratch), scratch_size) == .Ok)
		testing.expect(t, bytes.equal(decoded, pixels))
		for &pixel in decoded { pixel = 0 }
		testing.expect(t, decode_artery(raw_data(expected), stream_count, &artery_atlas, channels,
			raw_data(decoded), pixel_count, raw_data(scratch), scratch_size) == .Ok)
		testing.expect(t, bytes.equal(decoded, pixels))
	}
}
