#define GMD2_ENCODER_IMPLEMENTATION
#define GMD2_DECODER_IMPLEMENTATION
#include "gmd2.h"
#include "../include/gmd2_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <zstd.h>

static int archive_fail(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return -1;
}

static const char *codec_error(int status) {
    switch (status) {
    case GMD2_ERR_INVALID_ARGUMENT: return "invalid codec argument";
    case GMD2_ERR_INVALID_LAYOUT: return "invalid or incompatible atlas layout";
    case GMD2_ERR_INVALID_STREAM: return "invalid or truncated GMD2 stream";
    case GMD2_ERR_OUTPUT_TOO_SMALL: return "GMD2 output buffer is too small";
    case GMD2_ERR_SCRATCH_TOO_SMALL: return "GMD2 scratch buffer is too small";
    case GMD2_ERR_CHANNEL_MISMATCH: return "GMD2 stream requires RGBA8 output";
    default: return "GMD2 codec failed";
    }
}

int gmd2_archive_decode(const uint8_t *archive, size_t length, const Gmd2Metadata *layout,
                uint8_t **out, size_t *out_len, char *error, size_t error_len) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    size_t bound = layout ? gmd2_raw_bound(layout->width, layout->height, 3) : 0;
    if (!archive || !bound || !out || !out_len || length < GMD2_HEADER)
        return archive_fail(error, error_len, "invalid input or truncated GMD2 archive");
    if (memcmp(archive, "GMD2", 4) || archive[13] != 0 || archive[12] >= 24)
        return archive_fail(error, error_len, "unsupported GMD2 magic, flags, or map ID");
    if (gmd2_load32le(archive+4) != layout->width ||
        gmd2_load32le(archive+8) != layout->height)
        return archive_fail(error, error_len, "atlas dimensions disagree with layout");

    const uint8_t *frame = archive + GMD2_HEADER;
    size_t frame_len = length - GMD2_HEADER;
    size_t actual_frame = ZSTD_findFrameCompressedSize(frame, frame_len);
    if (ZSTD_isError(actual_frame) || actual_frame != frame_len)
        return archive_fail(error, error_len, "expected exactly one Zstd frame");

    size_t scratch_len = gmd2_decode_scratch_size(layout->width, layout->height, layout->glyph_count);
    if (!scratch_len)
        return archive_fail(error, error_len, "invalid scratch size");
    uint8_t *raw = (uint8_t*)malloc(bound);
    size_t result_len = gmd2_pixel_count(layout->width, layout->height) * 3;
    uint8_t *result = (uint8_t*)malloc(result_len);
    void *scratch = malloc(scratch_len);
    if (!raw || !result || !scratch) {
        free(scratch);
        free(raw);
        free(result);
        return archive_fail(error, error_len, "out of memory");
    }
    memcpy(raw, archive, GMD2_HEADER);
    raw[13] = GMD2_RAW;
    size_t body_len = ZSTD_decompress(raw+GMD2_HEADER, bound-GMD2_HEADER, frame, frame_len);
    int rc;
    if (ZSTD_isError(body_len)) {
        if (error && error_len)
            snprintf(error, error_len, "Zstd frame corrupt: %s", ZSTD_getErrorName(body_len));
        rc = -1;
    } else {
        Gmd2GlyphInput glyphs = {layout->glyphs, layout->glyph_count, GMD2_RECTANGLES, 0, 0};
        int status = gmd2_decode_pixels(raw, GMD2_HEADER+body_len, layout->width, layout->height,
                                        glyphs, 3, scratch, scratch_len, result, result_len);
        rc = status == GMD2_OK ? 0 : archive_fail(error, error_len, codec_error(status));
    }
    free(scratch);
    free(raw);
    if (rc) {
        free(result);
        return rc;
    }
    *out = result;
    *out_len = result_len;
    return 0;
}

int gmd2_archive_encode(const uint8_t *rgb, size_t rgb_len, const Gmd2Metadata *layout, int level,
                uint8_t **out, size_t *out_len, char *error, size_t error_len) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    size_t capacity = layout ? gmd2_encode_bound(layout->width, layout->height, 3) : 0;
    if (!rgb || !out || !out_len || !capacity ||
        rgb_len != gmd2_pixel_count(layout->width, layout->height) * 3)
        return archive_fail(error, error_len, "invalid input or pixel byte count");

    size_t scratch_len = gmd2_encode_scratch_size(layout->width, layout->height, layout->glyph_count);
    if (!scratch_len)
        return archive_fail(error, error_len, "invalid scratch size");
    uint8_t *raw = (uint8_t*)malloc(capacity);
    void *scratch = malloc(scratch_len);
    if (!raw || !scratch) {
        free(scratch);
        free(raw);
        return archive_fail(error, error_len, "out of memory");
    }
    Gmd2GlyphInput glyphs = {layout->glyphs, layout->glyph_count, GMD2_RECTANGLES, 0, 0};
    int32_t raw_len = gmd2_encode_pixels(rgb, rgb_len, layout->width, layout->height, glyphs, 3,
                                         scratch, scratch_len, raw, capacity);
    free(scratch);
    if (raw_len < 0) {
        free(raw);
        return archive_fail(error, error_len, codec_error(raw_len));
    }

    size_t body_len = raw_len - GMD2_HEADER;
    size_t bound = ZSTD_compressBound(body_len);
    if (!bound || ZSTD_isError(bound) || bound > SIZE_MAX-GMD2_HEADER) {
        free(raw);
        return archive_fail(error, error_len, "GMD2 body is too large for Zstd");
    }
    uint8_t *archive = (uint8_t*)malloc(GMD2_HEADER+bound);
    if (!archive) {
        free(raw);
        return archive_fail(error, error_len, "out of memory");
    }
    memcpy(archive, raw, GMD2_HEADER);
    archive[13] = 0;
    size_t comp_size = ZSTD_compress(archive+GMD2_HEADER, bound,
                                    raw+GMD2_HEADER, body_len, level);
    free(raw);
    if (ZSTD_isError(comp_size)) {
        free(archive);
        if (error && error_len)
            snprintf(error, error_len, "Zstd compression failed: %s",
                     ZSTD_getErrorName(comp_size));
        return -1;
    }
    *out = archive;
    *out_len = GMD2_HEADER+comp_size;
    return 0;
}
