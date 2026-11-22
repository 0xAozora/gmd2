//! Direct encoder functions.

use super::{ArteryAtlasView, AtlasView};
use std::ffi::{c_uint, c_void};

extern "C" {
    #[link_name = "gmd2_encode_bound"]
    pub fn encode_bound(width: u32, height: u32, channels: c_uint) -> usize;

    #[link_name = "gmd2_encode_scratch_size"]
    pub fn encode_scratch_size(width: u32, height: u32, glyph_count: usize) -> usize;

    #[link_name = "gmd2_encode"]
    pub fn encode(
        pixels: *const u8,
        length: usize,
        atlas: *const AtlasView,
        channels: c_uint,
        out: *mut u8,
        capacity: usize,
        scratch: *mut c_void,
        scratch_len: usize,
    ) -> i32;

    #[link_name = "gmd2_encode_artery"]
    pub fn encode_artery(
        pixels: *const u8,
        length: usize,
        atlas: *const ArteryAtlasView,
        channels: c_uint,
        out: *mut u8,
        capacity: usize,
        scratch: *mut c_void,
        scratch_len: usize,
    ) -> i32;
}
