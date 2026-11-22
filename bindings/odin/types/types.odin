package gmd2_types

import "core:c"

Status :: enum c.int {
	Ok = 0,
	Invalid_Argument = -1,
	Invalid_Layout = -2,
	Invalid_Stream = -3,
	Output_Too_Small = -4,
	Scratch_Too_Small = -5,
	Channel_Mismatch = -6,
}

// Glyph matches the caller's parsed msdf-atlas-gen JSON glyph.
Glyph :: struct {
	Index: u32,
	Advance: f32,
	PlaneBounds: Bounds,
	AtlasBounds: Bounds,
}

Bounds :: struct {
	Left, Bottom, Right, Top: f32,
}

// Artery_Glyph matches artery_font::Glyph<float>.
Artery_Glyph :: struct {
	Codepoint, Image: u32,
	PlaneBounds, ImageBounds: Bounds,
	Advance: Artery_Advance,
}

Artery_Advance :: struct {
	H, V: f32,
}

Y_Origin :: enum u32 {
	Top = 0,
	Bottom = 1,
}

Atlas_View :: struct {
	Width, Height: u32,
	Glyphs: [^]Glyph,
	Glyph_Count: u32,
	Y_Origin: Y_Origin,
}

Artery_Atlas_View :: struct {
	Width, Height: u32,
	Glyphs: [^]Artery_Glyph,
	Glyph_Count: u32,
	Image_Index: u32,
}
