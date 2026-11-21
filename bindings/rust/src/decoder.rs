//! Direct decoder functions.

use super::{ArteryAtlasView, AtlasView};
use std::ffi::{c_int, c_uint, c_void};

extern "C" {
    #[link_name = "gmd2_decode_size"]
    pub fn decode_size(data: *const u8, length: usize, channels: c_uint) -> usize;

    #[link_name = "gmd2_decode_scratch_size"]
    pub fn decode_scratch_size(width: u32, height: u32, glyph_count: usize) -> usize;

    #[link_name = "gmd2_decode"]
    pub fn decode(
        data: *const u8,
        length: usize,
        atlas: *const AtlasView,
        channels: c_uint,
        out: *mut u8,
        capacity: usize,
        scratch: *mut c_void,
        scratch_len: usize,
    ) -> c_int;

    #[link_name = "gmd2_decode_artery"]
    pub fn decode_artery(
        data: *const u8,
        length: usize,
        atlas: *const ArteryAtlasView,
        channels: c_uint,
        out: *mut u8,
        capacity: usize,
        scratch: *mut c_void,
        scratch_len: usize,
    ) -> c_int;
}
