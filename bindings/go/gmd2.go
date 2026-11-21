// Package gmd2 provides glyph records shared by the encoder and decoder.
package gmd2

import "gmd2/types"

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
