package gmd2

import (
	"bytes"
	"os"
	"testing"
	"unsafe"

	"gmd2/decoder"
	"gmd2/encoder"
)

func TestFixtures(t *testing.T) {
	json := Glyph{Index: 7, Advance: 0.75, AtlasBounds: Bounds{Left: 0.5, Right: 1.5, Top: 2}}
	top := json
	top.AtlasBounds.Top, top.AtlasBounds.Bottom = 0, 2
	artery := [2]ArteryGlyph{{Image: 1}, {Codepoint: 7, ImageBounds: json.AtlasBounds}}
	bottomAtlas := AtlasView{Width: 2, Height: 2, Glyphs: &json, GlyphCount: 1, YOrigin: OriginBottom}
	topAtlas := AtlasView{Width: 2, Height: 2, Glyphs: &top, GlyphCount: 1, YOrigin: OriginTop}
	arteryAtlas := ArteryAtlasView{Width: 2, Height: 2, Glyphs: &artery[0], GlyphCount: 2, ImageIndex: 0}
	for _, fixture := range []struct {
		name     string
		channels uint32
	}{{"rgb8", 3}, {"rgba8", 4}} {
		t.Run(fixture.name, func(t *testing.T) {
			expected, err := os.ReadFile("../../tests/fixtures/" + fixture.name + ".gmd2")
			if err != nil {
				t.Fatal(err)
			}
			channels := fixture.channels
			pixels := make([]byte, 4*channels)
			for i := range pixels {
				pixels[i] = byte(i * 13)
			}
			encoded := make([]byte, encoder.EncodeBound(2, 2, 0))
			decoded := make([]byte, decoder.DecodeSize(unsafe.SliceData(expected), uint(len(expected)), 0))
			scratch := make([]byte, encoder.EncodeScratchSize(2, 2, 2))
			if uint(len(scratch)) != decoder.DecodeScratchSize(2, 2, 2) {
				t.Fatal("scratch size differs between encoder and decoder")
			}
			scratchPtr := unsafe.SliceData(scratch)
			input, out := unsafe.SliceData(pixels), unsafe.SliceData(encoded)
			src, dst := unsafe.SliceData(expected), unsafe.SliceData(decoded)
			if written := encoder.Encode(input, uint(len(pixels)), bottomAtlas, 0,
				out, uint(len(encoded)), scratchPtr, uint(len(scratch))); written != int32(len(expected)) || !bytes.Equal(encoded[:written], expected) {
				t.Fatal("JSON glyph encoder differs from fixture")
			}
			if written := encoder.EncodeArtery(input, uint(len(pixels)), arteryAtlas, channels,
				out, uint(len(encoded)), scratchPtr, uint(len(scratch))); written != int32(len(expected)) || !bytes.Equal(encoded[:written], expected) {
				t.Fatal("Artery glyph encoder differs from fixture")
			}
			if decoder.Decode(src, uint(len(expected)), topAtlas, 0,
				dst, uint(len(decoded)), scratchPtr, uint(len(scratch))) != OK || !bytes.Equal(decoded, pixels) {
				t.Fatal("JSON glyph decoder differs from RGB fixture")
			}
			for i := range decoded {
				decoded[i] = 0
			}
			if decoder.DecodeArtery(src, uint(len(expected)), arteryAtlas, channels,
				dst, uint(len(decoded)), scratchPtr, uint(len(scratch))) != OK || !bytes.Equal(decoded, pixels) {
				t.Fatal("Artery glyph decoder differs from RGB fixture")
			}
		})
	}
}

func TestShortOutput(t *testing.T) {
	pixels := [3]byte{}
	out := [1]byte{}
	atlas := AtlasView{Width: 1, Height: 1, YOrigin: OriginTop}
	if encoder.Encode(&pixels[0], 3, atlas, 3, &out[0], 0, nil, 0) != ErrOutputTooSmall {
		t.Fatal("expected a capacity error")
	}
}
