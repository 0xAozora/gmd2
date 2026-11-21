// Package decoder binds the raw GMD2 decoder. Pointers must cover the stated
// lengths, and output must be writable and disjoint from input.
package decoder

/*
#include "../../../include/gmd2_decoder.h"

// Build the view in C so cgo receives the glyph pointer directly.
static inline int call_gmd2_decode(const uint8_t *data, size_t length,
        uint32_t width, uint32_t height, const Gmd2Glyph *glyphs, size_t glyph_count,
        uint32_t y_origin, unsigned channels, uint8_t *out, size_t capacity,
        void *scratch, size_t scratch_len) {
    const Gmd2AtlasView atlas = {width, height, glyphs, glyph_count, y_origin};
    return gmd2_decode(data, length, &atlas, channels, out, capacity,
        scratch, scratch_len);
}

static inline int call_gmd2_decode_artery(const uint8_t *data, size_t length,
        uint32_t width, uint32_t height, const Gmd2ArteryGlyph *glyphs, size_t glyph_count,
        uint32_t image_index, unsigned channels, uint8_t *out, size_t capacity,
        void *scratch, size_t scratch_len) {
    const Gmd2ArteryAtlasView atlas = {width, height, glyphs, glyph_count, image_index};
    return gmd2_decode_artery(data, length, &atlas, channels, out, capacity,
        scratch, scratch_len);
}
*/
import "C"

import (
	"unsafe"

	"gmd2/types"
)

type Bounds = types.Bounds
type Glyph = types.Glyph
type ArteryAdvance = types.ArteryAdvance
type ArteryGlyph = types.ArteryGlyph
type YOrigin = types.YOrigin
type AtlasView = types.AtlasView
type ArteryAtlasView = types.ArteryAtlasView

const (
	OriginTop    = types.OriginTop
	OriginBottom = types.OriginBottom

	OK                 = types.OK
	ErrInvalidArgument = types.ErrInvalidArgument
	ErrInvalidLayout   = types.ErrInvalidLayout
	ErrInvalidStream   = types.ErrInvalidStream
	ErrOutputTooSmall  = types.ErrOutputTooSmall
	ErrScratchTooSmall = types.ErrScratchTooSmall
	ErrChannelMismatch = types.ErrChannelMismatch
)

func DecodeSize(data *byte, length uint, channels uint32) uint {
	return uint(C.gmd2_decode_size((*C.uint8_t)(data), C.size_t(length), C.uint(channels)))
}

func DecodeScratchSize(width, height uint32, glyphCount uint) uint {
	return uint(C.gmd2_decode_scratch_size(C.uint32_t(width), C.uint32_t(height), C.size_t(glyphCount)))
}

func Decode(data *byte, length uint, atlas AtlasView, channels uint32,
	out *byte, capacity uint,
	scratch *byte, scratchLen uint) int32 {
	return int32(C.call_gmd2_decode((*C.uint8_t)(data), C.size_t(length), C.uint32_t(atlas.Width), C.uint32_t(atlas.Height),
		(*C.Gmd2Glyph)(unsafe.Pointer(atlas.Glyphs)), C.size_t(atlas.GlyphCount), C.uint32_t(atlas.YOrigin), C.uint(channels),
		(*C.uint8_t)(out), C.size_t(capacity), unsafe.Pointer(scratch), C.size_t(scratchLen)))
}

func DecodeArtery(data *byte, length uint, atlas ArteryAtlasView, channels uint32,
	out *byte, capacity uint,
	scratch *byte, scratchLen uint) int32 {
	return int32(C.call_gmd2_decode_artery((*C.uint8_t)(data), C.size_t(length), C.uint32_t(atlas.Width), C.uint32_t(atlas.Height),
		(*C.Gmd2ArteryGlyph)(unsafe.Pointer(atlas.Glyphs)), C.size_t(atlas.GlyphCount), C.uint32_t(atlas.ImageIndex), C.uint(channels),
		(*C.uint8_t)(out), C.size_t(capacity), unsafe.Pointer(scratch), C.size_t(scratchLen)))
}
