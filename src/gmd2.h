#ifndef GMD2_H
#define GMD2_H

#define GMD2_CLI_METADATA
#include "../include/gmd2_encoder.h"
#undef GMD2_CLI_METADATA

typedef struct {
    uint32_t width, height;
    size_t glyph_count;
    Gmd2GlyphRect *glyphs;
} Gmd2Metadata;

#ifdef __cplusplus
extern "C" {
#endif

/* Reads a.w, a.h, g[].i, and g[].b from FontLayoutMsgp. */
int gmd2_layout_from_msgpack(const uint8_t *data, size_t size,
                            Gmd2Metadata *layout, char *error, size_t error_len);
/* Read CLI JSON metadata into top-down coordinates. Release with gmd2_free_layout. */
int gmd2_layout_from_msdf_json(const char *data, size_t size, Gmd2Metadata *layout,
                              char *error, size_t error_len);
void gmd2_free_layout(Gmd2Metadata *layout);

/* Archive functions return 0 on success. The caller frees *out. */
int gmd2_archive_decode(const uint8_t *archive, size_t archive_len,
                const Gmd2Metadata *layout, uint8_t **out, size_t *out_len,
                char *error, size_t error_len);

int gmd2_archive_encode(const uint8_t *rgb, size_t rgb_len,
                const Gmd2Metadata *layout, int zstd_level,
                uint8_t **out, size_t *out_len,
                char *error, size_t error_len);

#ifdef __cplusplus
}
#endif
#endif
