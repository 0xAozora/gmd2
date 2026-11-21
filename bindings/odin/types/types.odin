package gmd2_types

import "core:c"

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
	Glyph_Count: c.size_t,
	Y_Origin: Y_Origin,
}

Artery_Atlas_View :: struct {
	Width, Height: u32,
	Glyphs: [^]Artery_Glyph,
	Glyph_Count: c.size_t,
	Image_Index: u32,
}
