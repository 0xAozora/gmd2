#define GMD2_ENCODER_IMPLEMENTATION
#define GMD2_DECODER_IMPLEMENTATION
#include "../include/gmd2_decoder.h"
#include "../src/gmd2.h"

#include <assert.h>
#include <stdio.h>

_Static_assert(sizeof(Gmd2Glyph) == 40, "parsed JSON glyph ABI");
_Static_assert(sizeof(Gmd2ArteryGlyph) == 48, "Artery binary32 glyph ABI");

static void test_atlas_rectangles(void) {
    Gmd2AtlasBounds bounds = {10.5f, 3.5f, 13.0f, 7.5f};
    Gmd2GlyphRect glyph;
    assert(gmd2_atlas_rectangle(bounds, 12, GMD2_ORIGIN_BOTTOM, &glyph));
    assert(glyph.x == 10 && glyph.y == 4 && glyph.width == 2 && glyph.height == 4);
    bounds.top = 4.5f;
    bounds.bottom = 8.5f;
    bounds.right = 14.0f;
    assert(gmd2_atlas_rectangle(bounds, 12, GMD2_ORIGIN_TOP, &glyph));
    assert(glyph.x == 10 && glyph.y == 4 && glyph.width == 4 && glyph.height == 4);
}

static void test_json(void) {
    const char bottom_json[] =
        "{\"atlas\":{\"width\":5,\"height\":4,\"yOrigin\":\"bottom\"},"
        "\"glyphs\":[{\"unicode\":32},{\"index\":7,\"atlasBounds\":"
        "{\"left\":1.5,\"bottom\":0.5,\"right\":3.5,\"top\":2.5}}]}";
    const char top_json[] =
        "{\"variants\":[{\"glyphs\":[{\"unicode\":32}]},{\"glyphs\":[{\"index\":7,"
        "\"atlasBounds\":{\"left\":1.5,\"top\":1.5,\"right\":3.5,\"bottom\":3.5}}]}],"
        "\"atlas\":{\"width\":5,\"height\":4,\"yOrigin\":\"top\"}}";
    Gmd2Metadata bottom, top;
    assert(!gmd2_layout_from_msdf_json(bottom_json, sizeof(bottom_json)-1, &bottom, NULL, 0));
    assert(!gmd2_layout_from_msdf_json(top_json, sizeof(top_json)-1, &top, NULL, 0));
    assert(bottom.width == top.width && bottom.height == top.height);
    assert(bottom.glyph_count == 2 && top.glyph_count == 2);
    assert(bottom.glyphs[0].width == 0 && top.glyphs[0].width == 0);
    Gmd2GlyphRect a = bottom.glyphs[1], b = top.glyphs[1];
    assert(a.index == 7 && b.index == 7 && a.source_order == 1 && b.source_order == 1);
    assert(a.x == 1 && a.y == 1 && a.width == 2 && a.height == 2);
    assert(a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height);
    gmd2_free_layout(&bottom);
    gmd2_free_layout(&top);
    assert(gmd2_layout_from_msdf_json(top_json, sizeof(top_json)-2, &top, NULL, 0));
    assert(top.glyphs == NULL && top.glyph_count == 0);
}

int main(void) {
    test_atlas_rectangles();
    test_json();
    enum { W = 5, H = 4, N = W*H };
    const Gmd2Glyph glyphs[] = {
        {.index = 7, .atlas_bounds = {1, 3, 3, 1}},
        {.index = 9, .atlas_bounds = {3, 3, 5, 1}},
    };
    const size_t glyph_count = sizeof(glyphs) / sizeof(glyphs[0]);
    const Gmd2AtlasView atlas = {W, H, glyphs, glyph_count, GMD2_ORIGIN_TOP};
    const Gmd2AtlasView empty_atlas = {W, H, NULL, 0, GMD2_ORIGIN_TOP};
    uint8_t rgb[3*N], rgba[4*N], decoded[4*N];
    uint8_t encoded[18+5*N], other[sizeof(encoded)];
    int32_t rgb_len, rgba_len;

    for (size_t i = 0; i < N; i++) {
        for (size_t c = 0; c < 3; c++) rgb[3*i+c] = (uint8_t)(7*i+31*c);
        memcpy(rgba+4*i, rgb+3*i, 3);
        rgba[4*i+3] = 255;
    }
    assert(gmd2_encode_bound(W, H, 3) == 18 + 4*N);
    assert(gmd2_encode_bound(W, H, 4) == sizeof(encoded));
    assert(gmd2_encode_bound(W, H, 0) == sizeof(encoded));
    assert(!gmd2_encode_bound(32768, 16384, 3));

    _Alignas(uint32_t) uint8_t storage[1 + sizeof(Gmd2GlyphRect) * (sizeof(glyphs) / sizeof(glyphs[0])) + (N + 7) / 8];
    uint8_t *scratch = storage + 1;
    size_t scratch_len = gmd2_decode_scratch_size(W, H, glyph_count);
    assert(scratch_len == sizeof(storage) - 1);
    assert(scratch_len == gmd2_encode_scratch_size(W, H, glyph_count));
    assert(!gmd2_decode_scratch_size(0, H, glyph_count));
    assert(!gmd2_encode_scratch_size(W, H, SIZE_MAX));
    memset(storage, 0xff, sizeof(storage));

    rgb_len = gmd2_encode(rgb, sizeof(rgb), &atlas, 0,
                        encoded, sizeof(encoded), scratch, scratch_len);
    assert(rgb_len > 0);
    rgba_len = gmd2_encode(rgba, sizeof(rgba), &atlas, 0,
                        other, sizeof(other), scratch, scratch_len);
    assert(rgba_len > 0);
    assert(rgb_len == rgba_len && !memcmp(encoded, other, rgb_len));
    assert(gmd2_decode_size(encoded, rgb_len, 0) == sizeof(rgb));
    assert(gmd2_decode_size(encoded, rgb_len, 4) == sizeof(rgba));
    assert(!gmd2_decode_size(encoded, 17, 0));
    assert(!gmd2_decode_size(encoded, rgb_len, 1));

    memset(decoded, 0, sizeof(decoded));
    assert(!gmd2_decode(encoded, rgb_len, &atlas, 0,
                        decoded, sizeof(decoded), scratch, scratch_len));
    assert(!memcmp(rgb, decoded, sizeof(rgb)) && decoded[sizeof(rgb)] == 0);
    assert(!gmd2_decode(encoded, rgb_len, &atlas, 4,
                        decoded, sizeof(decoded), scratch, scratch_len));
    assert(!memcmp(rgba, decoded, sizeof(rgba)));

    for (size_t i = 0; i < N; i++) rgba[4*i+3] = (uint8_t)(i*13);
    rgba_len = gmd2_encode(rgba, sizeof(rgba), &atlas, 0,
                        encoded, sizeof(encoded), scratch, scratch_len);
    assert(rgba_len > 0);
    assert(rgba_len == rgb_len + N);
    assert(gmd2_decode_size(encoded, 18, 0) == sizeof(rgba));
    assert(!gmd2_decode_size(encoded, rgba_len, 3));
    assert(!gmd2_decode(encoded, rgba_len, &atlas, 0,
                        decoded, sizeof(decoded), scratch, scratch_len));
    assert(!memcmp(rgba, decoded, sizeof(rgba)) && storage[0] == 0xff);

    assert(gmd2_encode(rgba, sizeof(rgba), &atlas, 4,
                        other, sizeof(other), scratch, scratch_len - 1) == GMD2_ERR_SCRATCH_TOO_SMALL);
    assert(gmd2_encode(rgba, sizeof(rgba) - 1, &atlas, 0,
                        other, sizeof(other), scratch, scratch_len) == GMD2_ERR_INVALID_ARGUMENT);
    assert(gmd2_encode(rgba, sizeof(rgba), &atlas, 4,
                        other, rgba_len - 1, scratch, scratch_len) == GMD2_ERR_OUTPUT_TOO_SMALL);

    memset(decoded, 0, sizeof(decoded));
    assert(gmd2_decode(encoded, rgba_len, &atlas, 4,
                        decoded, sizeof(decoded), scratch, scratch_len - 1) == GMD2_ERR_SCRATCH_TOO_SMALL);
    assert(decoded[3] == 0 && decoded[sizeof(decoded) - 1] == 0);
    assert(gmd2_decode(encoded, rgba_len, &atlas, 4,
                        decoded, sizeof(decoded), NULL, scratch_len) == GMD2_ERR_SCRATCH_TOO_SMALL);
    assert(gmd2_decode(encoded, rgba_len, &atlas, 3,
                        decoded, sizeof(decoded), scratch, scratch_len) == GMD2_ERR_CHANNEL_MISMATCH);
    assert(gmd2_decode(encoded, rgba_len - 1, &atlas, 4,
                        decoded, sizeof(decoded), scratch, scratch_len) == GMD2_ERR_INVALID_STREAM);
    assert(gmd2_decode(encoded, rgba_len, &atlas, 4,
                        decoded, sizeof(decoded) - 1, scratch, scratch_len) == GMD2_ERR_OUTPUT_TOO_SMALL);
    encoded[13] = 0;
    assert(!gmd2_decode_size(encoded, rgba_len, 0));
    assert(gmd2_decode(encoded, rgba_len, &atlas, 0, decoded, sizeof(decoded),
                       scratch, scratch_len) == GMD2_ERR_INVALID_STREAM);

    rgb_len = gmd2_encode(rgb, sizeof(rgb), &empty_atlas, 3,
                        encoded, sizeof(encoded), scratch, scratch_len);
    assert(rgb_len > 0);
    assert(!gmd2_decode(encoded, rgb_len, &empty_atlas, 3,
                        decoded, sizeof(rgb), scratch, scratch_len));
    assert(!memcmp(rgb, decoded, sizeof(rgb)));

    assert(gmd2_encode(rgb, sizeof(rgb), NULL, 3, encoded, sizeof(encoded),
                       scratch, scratch_len) == GMD2_ERR_INVALID_ARGUMENT);
    assert(gmd2_decode(encoded, rgb_len, NULL, 3, decoded, sizeof(rgb),
                       scratch, scratch_len) == GMD2_ERR_INVALID_ARGUMENT);

    assert(gmd2_decode(NULL, 0, &empty_atlas, 3, decoded, sizeof(rgb),
                       scratch, scratch_len) == GMD2_ERR_INVALID_ARGUMENT);
    Gmd2AtlasView bad_atlas = atlas;
    bad_atlas.y_origin = 2;
    assert(gmd2_encode(rgb, sizeof(rgb), &bad_atlas, 3, encoded, sizeof(encoded),
                       scratch, scratch_len) == GMD2_ERR_INVALID_LAYOUT);
    assert(gmd2_decode(encoded, rgb_len, &bad_atlas, 3, decoded, sizeof(rgb),
                       scratch, scratch_len) == GMD2_ERR_INVALID_LAYOUT);
    puts("OK: raw RGB8 and RGBA8, channel detection, scratch sizes, and msdf-atlas-gen JSON");
    return 0;
}
