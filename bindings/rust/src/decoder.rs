//! Direct decoder functions.

use super::{ArteryAtlasView, AtlasView};
use std::ffi::{c_int, c_uint, c_void};

extern "C" {
    #[link_name = "gmd2_decode_size"]
    pub fn decode_size(data: *const u8, length: u32, channels: c_uint) -> u32;

    #[link_name = "gmd2_decode_scratch_size"]
    pub fn decode_scratch_size(width: u32, height: u32, glyph_count: u32) -> u32;

    #[link_name = "gmd2_decode"]
    pub fn decode(
        data: *const u8,
        length: u32,
        atlas: *const AtlasView,
        channels: c_uint,
        out: *mut u8,
        capacity: u32,
        scratch: *mut c_void,
        scratch_len: u32,
    ) -> c_int;

    #[link_name = "gmd2_decode_artery"]
    pub fn decode_artery(
        data: *const u8,
        length: u32,
        atlas: *const ArteryAtlasView,
        channels: c_uint,
        out: *mut u8,
        capacity: u32,
        scratch: *mut c_void,
        scratch_len: u32,
    ) -> c_int;
}
