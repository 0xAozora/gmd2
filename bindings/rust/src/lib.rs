//! Direct C bindings for raw GMD2. Pixel and glyph pointers must cover the stated
//! lengths and remain valid during the call. Output must be writable and disjoint
//! from input. The glyph count is the number of records.

#[derive(Clone, Copy, Debug, Default)]
#[repr(C)]
pub struct Bounds {
    pub left: f32,
    pub bottom: f32,
    pub right: f32,
    pub top: f32,
}

/// Matches a parsed msdf-atlas-gen JSON glyph.
#[derive(Clone, Copy, Debug, Default)]
#[repr(C)]
pub struct Glyph {
    pub index: u32,
    pub advance: f32,
    pub plane_bounds: Bounds,
    pub atlas_bounds: Bounds,
}

#[derive(Clone, Copy, Debug, Default)]
#[repr(C)]
pub struct ArteryAdvance {
    pub h: f32,
    pub v: f32,
}

/// Matches artery_font::Glyph<float>.
#[derive(Clone, Copy, Debug, Default)]
#[repr(C)]
pub struct ArteryGlyph {
    pub codepoint: u32,
    pub image: u32,
    pub plane_bounds: Bounds,
    pub image_bounds: Bounds,
    pub advance: ArteryAdvance,
}

pub const ORIGIN_TOP: u32 = 0;
pub const ORIGIN_BOTTOM: u32 = 1;

/// Borrows parsed glyph records for the duration of a codec call.
#[repr(C)]
pub struct AtlasView {
    pub width: u32,
    pub height: u32,
    pub glyphs: *const Glyph,
    pub glyph_count: usize,
    pub y_origin: u32,
}

/// Selects one atlas image from the given Artery glyph records.
#[repr(C)]
pub struct ArteryAtlasView {
    pub width: u32,
    pub height: u32,
    pub glyphs: *const ArteryGlyph,
    pub glyph_count: usize,
    pub image_index: u32,
}

#[cfg(feature = "encoder")]
pub mod encoder;
#[cfg(feature = "encoder")]
pub use encoder::{encode, encode_artery, encode_bound, encode_scratch_size};
#[cfg(feature = "decoder")]
pub mod decoder;
#[cfg(feature = "decoder")]
pub use decoder::{decode, decode_artery, decode_scratch_size, decode_size};

#[cfg(all(test, any(feature = "encoder", feature = "decoder")))]
mod tests {
    use super::*;
    use std::ptr::null_mut;

    const BOTTOM_BOUNDS: Bounds = Bounds {
        left: 0.5,
        bottom: 0.0,
        right: 1.5,
        top: 2.0,
    };
    const FIXTURES: [(u32, &[u8]); 2] = [
        (3, include_bytes!("../../../tests/fixtures/rgb8.gmd2")),
        (4, include_bytes!("../../../tests/fixtures/rgba8.gmd2")),
    ];

    fn artery_glyphs() -> [ArteryGlyph; 2] {
        [
            ArteryGlyph {
                image: 1,
                ..Default::default()
            },
            ArteryGlyph {
                codepoint: 7,
                image_bounds: BOTTOM_BOUNDS,
                ..Default::default()
            },
        ]
    }

    #[cfg(feature = "encoder")]
    #[test]
    fn encode_fixtures() {
        let json = Glyph {
            index: 7,
            atlas_bounds: BOTTOM_BOUNDS,
            ..Default::default()
        };
        let atlas = AtlasView {
            width: 2,
            height: 2,
            glyphs: &json,
            glyph_count: 1,
            y_origin: ORIGIN_BOTTOM,
        };
        let artery = artery_glyphs();
        let artery_atlas = ArteryAtlasView {
            width: 2,
            height: 2,
            glyphs: artery.as_ptr(),
            glyph_count: artery.len(),
            image_index: 0,
        };
        for (channels, expected) in FIXTURES {
            let pixels: Vec<u8> = (0..4 * channels).map(|i| (i * 13) as u8).collect();
            let mut encoded = vec![0; unsafe { encode_bound(2, 2, 0) }];
            let mut scratch = vec![0_u8; unsafe { encode_scratch_size(2, 2, 2) }];
            let mut written = 0;
            let status = unsafe {
                encode(
                    pixels.as_ptr(),
                    pixels.len(),
                    &atlas,
                    0,
                    encoded.as_mut_ptr(),
                    encoded.len(),
                    &mut written,
                    scratch.as_mut_ptr().cast(),
                    scratch.len(),
                    null_mut(),
                    0,
                )
            };
            assert_eq!(status, 0);
            assert_eq!(&encoded[..written], expected);
            let status = unsafe {
                encode_artery(
                    pixels.as_ptr(),
                    pixels.len(),
                    &artery_atlas,
                    channels,
                    encoded.as_mut_ptr(),
                    encoded.len(),
                    &mut written,
                    scratch.as_mut_ptr().cast(),
                    scratch.len(),
                    null_mut(),
                    0,
                )
            };
            assert_eq!(status, 0);
            assert_eq!(&encoded[..written], expected);
        }
    }

    #[cfg(feature = "decoder")]
    #[test]
    fn decode_fixtures() {
        let json = Glyph {
            index: 7,
            atlas_bounds: Bounds {
                bottom: 2.0,
                top: 0.0,
                ..BOTTOM_BOUNDS
            },
            ..Default::default()
        };
        let atlas = AtlasView {
            width: 2,
            height: 2,
            glyphs: &json,
            glyph_count: 1,
            y_origin: ORIGIN_TOP,
        };
        let artery = artery_glyphs();
        let artery_atlas = ArteryAtlasView {
            width: 2,
            height: 2,
            glyphs: artery.as_ptr(),
            glyph_count: artery.len(),
            image_index: 0,
        };
        for (channels, encoded) in FIXTURES {
            let expected: Vec<u8> = (0..4 * channels).map(|i| (i * 13) as u8).collect();
            let mut decoded = vec![0; unsafe { decode_size(encoded.as_ptr(), encoded.len(), 0) }];
            let mut scratch = vec![0_u8; unsafe { decode_scratch_size(2, 2, 2) }];
            let status = unsafe {
                decode(
                    encoded.as_ptr(),
                    encoded.len(),
                    &atlas,
                    0,
                    decoded.as_mut_ptr(),
                    decoded.len(),
                    scratch.as_mut_ptr().cast(),
                    scratch.len(),
                    null_mut(),
                    0,
                )
            };
            assert_eq!(status, 0);
            assert_eq!(decoded, expected);
            decoded.fill(0);
            let status = unsafe {
                decode_artery(
                    encoded.as_ptr(),
                    encoded.len(),
                    &artery_atlas,
                    channels,
                    decoded.as_mut_ptr(),
                    decoded.len(),
                    scratch.as_mut_ptr().cast(),
                    scratch.len(),
                    null_mut(),
                    0,
                )
            };
            assert_eq!(status, 0);
            assert_eq!(decoded, expected);
        }
    }
}
