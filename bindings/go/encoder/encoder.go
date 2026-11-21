// Package encoder binds the raw GMD2 encoder. Pointers must cover the stated
// lengths, and output must be writable and disjoint from input.
package encoder

/*
#include "../../../include/gmd2_encoder.h"

// Build the view in C so cgo receives the glyph pointer directly.
static inline int call_gmd2_encode(const uint8_t *pixels, size_t length,
        uint32_t width, uint32_t height, const Gmd2Glyph *glyphs, size_t glyph_count,
        uint32_t y_origin, unsigned channels, uint8_t *out, size_t capacity,
        size_t *written, void *scratch, size_t scratch_len) {
    const Gmd2AtlasView atlas = {width, height, glyphs, glyph_count, y_origin};
    return gmd2_encode(pixels, length, &atlas, channels, out, capacity,
        written, scratch, scratch_len);
}

static inline int call_gmd2_encode_artery(const uint8_t *pixels, size_t length,
        uint32_t width, uint32_t height, const Gmd2ArteryGlyph *glyphs, size_t glyph_count,
        uint32_t image_index, unsigned channels, uint8_t *out, size_t capacity,
        size_t *written, void *scratch, size_t scratch_len) {
    const Gmd2ArteryAtlasView atlas = {width, height, glyphs, glyph_count, image_index};
    return gmd2_encode_artery(pixels, length, &atlas, channels, out, capacity,
        written, scratch, scratch_len);
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

func EncodeBound(width, height, channels uint32) uint {
	return uint(C.gmd2_encode_bound(C.uint32_t(width), C.uint32_t(height), C.uint(channels)))
}

func EncodeScratchSize(width, height uint32, glyphCount uint) uint {
	return uint(C.gmd2_encode_scratch_size(C.uint32_t(width), C.uint32_t(height), C.size_t(glyphCount)))
}

func Encode(pixels *byte, length uint, atlas AtlasView, channels uint32,
	out *byte, capacity uint, written *uint,
	scratch *byte, scratchLen uint) int32 {
	return int32(C.call_gmd2_encode((*C.uint8_t)(pixels), C.size_t(length), C.uint32_t(atlas.Width), C.uint32_t(atlas.Height),
		(*C.Gmd2Glyph)(unsafe.Pointer(atlas.Glyphs)), C.size_t(atlas.GlyphCount), C.uint32_t(atlas.YOrigin), C.uint(channels),
		(*C.uint8_t)(out), C.size_t(capacity), (*C.size_t)(unsafe.Pointer(written)), unsafe.Pointer(scratch), C.size_t(scratchLen)))
}

func EncodeArtery(pixels *byte, length uint, atlas ArteryAtlasView, channels uint32,
	out *byte, capacity uint, written *uint,
	scratch *byte, scratchLen uint) int32 {
	return int32(C.call_gmd2_encode_artery((*C.uint8_t)(pixels), C.size_t(length), C.uint32_t(atlas.Width), C.uint32_t(atlas.Height),
		(*C.Gmd2ArteryGlyph)(unsafe.Pointer(atlas.Glyphs)), C.size_t(atlas.GlyphCount), C.uint32_t(atlas.ImageIndex), C.uint(channels),
		(*C.uint8_t)(out), C.size_t(capacity), (*C.size_t)(unsafe.Pointer(written)), unsafe.Pointer(scratch), C.size_t(scratchLen)))
}
