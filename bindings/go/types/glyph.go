package types

type YOrigin uint32

const (
	OriginTop YOrigin = iota
	OriginBottom
)

// Glyph matches a parsed msdf-atlas-gen JSON glyph.
type Glyph struct {
	Index       uint32
	Advance     float32
	PlaneBounds Bounds
	AtlasBounds Bounds
}

type Bounds struct {
	Left, Bottom, Right, Top float32
}

// ArteryGlyph matches artery_font::Glyph<float>.
type ArteryGlyph struct {
	Codepoint, Image         uint32
	PlaneBounds, ImageBounds Bounds
	Advance                  ArteryAdvance
}

type ArteryAdvance struct {
	H, V float32
}

// AtlasView borrows the glyph array for an encode or decode call.
type AtlasView struct {
	Width, Height uint32
	Glyphs        *Glyph
	GlyphCount    uint32
	YOrigin       YOrigin
}

// ArteryAtlasView selects one atlas image from the given glyph records.
type ArteryAtlasView struct {
	Width, Height uint32
	Glyphs        *ArteryGlyph
	GlyphCount    uint32
	ImageIndex    uint32
}
