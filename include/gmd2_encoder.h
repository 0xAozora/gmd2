/* Generated from src/gmd2_codec.in. */
#ifndef GMD2_ENCODER_H
#define GMD2_ENCODER_H
#include <stddef.h>
#include <stdint.h>

#ifndef GMD2_GLYPH_DEFINED
#define GMD2_GLYPH_DEFINED

typedef struct {
    float left, bottom, right, top;
} Gmd2AtlasBounds;

/* Layout of a parsed msdf-atlas-gen JSON glyph. */
typedef struct {
    uint32_t index;
    float advance;
    Gmd2AtlasBounds plane_bounds, atlas_bounds;
} Gmd2Glyph;

/* Matches artery_font::Glyph<float>. */
typedef struct {
    uint32_t codepoint, image;
    Gmd2AtlasBounds plane_bounds, image_bounds;
    struct { float h, v; } advance;
} Gmd2ArteryGlyph;

enum { GMD2_ORIGIN_TOP = 0, GMD2_ORIGIN_BOTTOM = 1 };

enum {
    GMD2_OK = 0,
    GMD2_ERR_INVALID_ARGUMENT = -1,
    GMD2_ERR_INVALID_LAYOUT = -2,
    GMD2_ERR_INVALID_STREAM = -3,
    GMD2_ERR_OUTPUT_TOO_SMALL = -4,
    GMD2_ERR_SCRATCH_TOO_SMALL = -5,
    GMD2_ERR_CHANNEL_MISMATCH = -6
};

/* Views borrow the glyph array. NULL is valid when glyph_count is zero. */
typedef struct {
    uint32_t width, height;
    const Gmd2Glyph *glyphs;
    size_t glyph_count;
    uint32_t y_origin;
} Gmd2AtlasView;

typedef struct {
    uint32_t width, height;
    const Gmd2ArteryGlyph *glyphs;
    size_t glyph_count;
    uint32_t image_index;
} Gmd2ArteryAtlasView;

#endif

#if defined(GMD2_ENCODER_IMPLEMENTATION) || defined(GMD2_DECODER_IMPLEMENTATION) || defined(GMD2_CLI_METADATA)
#ifndef GMD2_GLYPH_RECT_DEFINED
#define GMD2_GLYPH_RECT_DEFINED
typedef struct {
    uint32_t index;
    int32_t x, y; /* Top-left in the top-down atlas. */
    uint32_t width, height;
    uint32_t source_order;
} Gmd2GlyphRect;
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Define GMD2_ENCODER_IMPLEMENTATION before including this header in one translation unit.
 * Use disjoint buffers and tightly packed, top-down RGB8 or RGBA8 pixels.
 * Pass channels 0 to infer the format from pixels_len.
 * Encoding returns bytes written or a negative GMD2_ERR_* code. */
/* Worst-case bytes (RGBA8 for channels 0), or 0 if invalid or above INT32_MAX. */
size_t gmd2_encode_bound(uint32_t width, uint32_t height, unsigned channels);
/* Required scratch bytes, or 0 on error. Scratch can have any alignment. */
size_t gmd2_encode_scratch_size(uint32_t width, uint32_t height, size_t glyph_count);
int32_t gmd2_encode(const uint8_t *pixels, size_t pixels_len, const Gmd2AtlasView *atlas,
                    unsigned channels, uint8_t *out, size_t capacity,
                    void *scratch, size_t scratch_len);
int32_t gmd2_encode_artery(const uint8_t *pixels, size_t pixels_len, const Gmd2ArteryAtlasView *atlas,
                           unsigned channels, uint8_t *out, size_t capacity,
                           void *scratch, size_t scratch_len);

#ifdef __cplusplus
}
#endif

#ifdef GMD2_ENCODER_IMPLEMENTATION
#ifndef GMD2_COMMON_IMPLEMENTED
#define GMD2_COMMON_IMPLEMENTED

#include <limits.h>
#include <string.h>

enum {
    GMD2_HEADER = 18,
    GMD2_RAW = 1,
    GMD2_ALPHA = 2,
    GMD2_ALPHA_MEDIAN = 4,
    GMD2_ESCAPE_SYMBOL = 4
};

static size_t gmd2_pixel_count(uint32_t width, uint32_t height) {
    uint64_t count = (uint64_t)width * height;
    if (count > UINT32_MAX || count > SIZE_MAX / sizeof(uint32_t)) return 0;
    return (size_t)count;
}

typedef struct {
    uint32_t width;
    size_t glyph_count;
    uint8_t *glyphs;
    uint8_t *covered;
} Gmd2Traversal;

static size_t gmd2_scratch_size(uint32_t width, uint32_t height, size_t glyph_count) {
    size_t count = gmd2_pixel_count(width, height);
    size_t covered_bytes = (count + 7) / 8;
    if (!count || glyph_count >= UINT32_MAX ||
        glyph_count > (SIZE_MAX - covered_bytes) / sizeof(Gmd2GlyphRect))
        return 0;
    return glyph_count * sizeof(Gmd2GlyphRect) + covered_bytes;
}

typedef struct {
    size_t next_glyph;
    uint32_t x, y;
    uint32_t pos; /* Glyph row start or next position to scan outside glyphs. */
    uint32_t glyph_width, glyph_height;
} Gmd2Cursor;

typedef struct {
    uint32_t pos, left, above, upper_left, above2;
} Gmd2Pixel;

typedef enum { GMD2_RECTANGLES, GMD2_JSON_GLYPHS, GMD2_ARTERY_GLYPHS } Gmd2GlyphFormat;
typedef struct {
    const void *glyphs;
    size_t count;
    Gmd2GlyphFormat format;
    uint32_t y_origin, image_index;
} Gmd2GlyphInput;

/* Round to nearest, ties to even. */
static int gmd2_round_extent(double extent, uint32_t *out) {
    if (!(extent >= -0.5 && extent < (double)UINT32_MAX + 0.5))
        return 0;
    uint32_t whole = extent <= 0 ? 0 : (uint32_t)extent;
    double fraction = extent - whole;
    *out = whole + (fraction > 0.5 || (fraction == 0.5 && (whole & 1)));
    return 1;
}

static int gmd2_atlas_rectangle(Gmd2AtlasBounds bounds, uint32_t height,
                                uint32_t y_origin, Gmd2GlyphRect *out) {
    double left = bounds.left, top = bounds.top;
    if (!(left >= INT32_MIN && left < (double)INT32_MAX + 1 &&
          top >= -(double)UINT32_MAX && top <= 2.0 * UINT32_MAX))
        return 0;
    int32_t x = (int32_t)left;
    int64_t y = (int64_t)top;
    double extent = (double)bounds.bottom - top;
    if (y_origin == GMD2_ORIGIN_BOTTOM) {
        y = (int64_t)height - (y + (y < top));
        extent = -extent;
    } else {
        y -= y > top;
    }
    if (y < INT32_MIN || y > INT32_MAX ||
        !gmd2_round_extent((double)bounds.right - left, &out->width) ||
        !gmd2_round_extent(extent, &out->height))
        return 0;
    out->x = x - (x > left);
    out->y = (int32_t)y;
    return 1;
}

static int gmd2_glyph_cmp(const Gmd2GlyphRect *a, const Gmd2GlyphRect *b) {
    if (a->width!=b->width) return a->width>b->width?-1:1;
    if (a->height!=b->height) return a->height<b->height?-1:1;
    if (a->index!=b->index) return a->index<b->index?-1:1;
    return a->source_order<b->source_order?-1:a->source_order>b->source_order;
}

/* Scratch may be unaligned. */
static inline Gmd2GlyphRect gmd2_get_glyph(const uint8_t *glyphs, size_t index) {
    Gmd2GlyphRect glyph;
    memcpy(&glyph, glyphs + index * sizeof(glyph), sizeof(glyph));
    return glyph;
}

static inline void gmd2_set_glyph(uint8_t *glyphs, size_t index, Gmd2GlyphRect glyph) {
    memcpy(glyphs + index * sizeof(glyph), &glyph, sizeof(glyph));
}

static void gmd2_sift_glyph(uint8_t *glyphs, size_t root, size_t count) {
    Gmd2GlyphRect glyph = gmd2_get_glyph(glyphs, root);
    while (root < count / 2) {
        size_t child = 2 * root + 1;
        Gmd2GlyphRect larger = gmd2_get_glyph(glyphs, child);
        if (child + 1 < count) {
            Gmd2GlyphRect right = gmd2_get_glyph(glyphs, child + 1);
            if (gmd2_glyph_cmp(&larger, &right) < 0) {
                child++;
                larger = right;
            }
        }
        if (gmd2_glyph_cmp(&glyph, &larger) >= 0)
            break;
        gmd2_set_glyph(glyphs, root, larger);
        root = child;
    }
    gmd2_set_glyph(glyphs, root, glyph);
}

/* Heapsort uses only the supplied scratch. */
static void gmd2_sort_glyphs(uint8_t *glyphs, size_t count) {
    for (size_t parent = count / 2; parent > 0;)
        gmd2_sift_glyph(glyphs, --parent, count);
    while (count > 1) {
        Gmd2GlyphRect last = gmd2_get_glyph(glyphs, --count);
        gmd2_set_glyph(glyphs, count, gmd2_get_glyph(glyphs, 0));
        gmd2_set_glyph(glyphs, 0, last);
        gmd2_sift_glyph(glyphs, 0, count);
    }
}

static int gmd2_setup_traversal(uint32_t w, uint32_t h,
                                Gmd2GlyphInput input, void *scratch, size_t scratch_len,
                                Gmd2Traversal *t) {
    size_t count = gmd2_pixel_count(w, h);
    size_t glyph_count = input.count;
    if (!count || (glyph_count && !input.glyphs) ||
        (input.format == GMD2_JSON_GLYPHS && input.y_origin > GMD2_ORIGIN_BOTTOM))
        return GMD2_ERR_INVALID_LAYOUT;
    size_t glyph_size = input.format == GMD2_JSON_GLYPHS ? sizeof(Gmd2Glyph) :
                        input.format == GMD2_ARTERY_GLYPHS ? sizeof(Gmd2ArteryGlyph) :
                        sizeof(Gmd2GlyphRect);
    size_t scratch_size = gmd2_scratch_size(w, h, glyph_count);
    if (!scratch_size || glyph_count > SIZE_MAX/glyph_size)
        return GMD2_ERR_INVALID_LAYOUT;
    if (!scratch || scratch_len < scratch_size)
        return GMD2_ERR_SCRATCH_TOO_SMALL;
    size_t glyph_bytes = glyph_count * sizeof(Gmd2GlyphRect);
    size_t covered_bytes = (count + 7) / 8;
    t->glyphs = (uint8_t*)scratch;
    t->width = w;
    t->glyph_count = 0;
    t->covered = t->glyphs + glyph_bytes;
    memset(t->covered, 0, covered_bytes);

    for (size_t i=0;i<glyph_count;i++) {
        Gmd2GlyphRect g;
        if (input.format == GMD2_RECTANGLES) {
            g = ((const Gmd2GlyphRect*)input.glyphs)[i];
        } else {
            Gmd2AtlasBounds bounds;
            if (input.format == GMD2_JSON_GLYPHS) {
                Gmd2Glyph record;
                memcpy(&record, (const uint8_t*)input.glyphs + i * sizeof(record), sizeof(record));
                g.index = record.index;
                bounds = record.atlas_bounds;
            } else {
                Gmd2ArteryGlyph record;
                memcpy(&record, (const uint8_t*)input.glyphs + i * sizeof(record), sizeof(record));
                if (record.image != input.image_index) continue;
                g.index = record.codepoint;
                bounds = record.image_bounds;
            }
            if (!gmd2_atlas_rectangle(bounds, h, input.y_origin, &g)) {
                return GMD2_ERR_INVALID_LAYOUT;
            }
            g.source_order = (uint32_t)i;
        }
        if (!g.width || !g.height) continue;
        if (g.x<0||g.y<0||(uint64_t)(uint32_t)g.x+g.width>w||
            (uint64_t)(uint32_t)g.y+g.height>h) {
            return GMD2_ERR_INVALID_LAYOUT;
        }
        for (uint32_t y = 0; y < g.height; y++) {
            uint32_t row = ((uint32_t)g.y + y) * w + (uint32_t)g.x;
            uint32_t last = row + g.width - 1;
            uint32_t byte = row / 8, last_byte = last / 8;
            /* Only the first and last bytes can be partially covered. */
            uint8_t mask = (uint8_t)(UINT8_MAX << (row % 8));
            for (; byte < last_byte; byte++) {
                if (t->covered[byte] & mask) return GMD2_ERR_INVALID_LAYOUT;
                t->covered[byte] |= mask;
                mask = UINT8_MAX;
            }
            mask &= (uint8_t)(UINT8_MAX >> (7 - last % 8));
            if (t->covered[byte] & mask) return GMD2_ERR_INVALID_LAYOUT;
            t->covered[byte] |= mask;
        }
        gmd2_set_glyph(t->glyphs, t->glyph_count++, g);
    }
    gmd2_sort_glyphs(t->glyphs, t->glyph_count);
    return GMD2_OK;
}

static void gmd2_next_glyph(const Gmd2Traversal *t, Gmd2Cursor *cursor) {
    cursor->y = 0;
    if (cursor->next_glyph == t->glyph_count) {
        /* Start the row-major scan for pixels outside glyphs. */
        cursor->glyph_width = 0;
        cursor->pos = 0;
        return;
    }
    Gmd2GlyphRect glyph = gmd2_get_glyph(t->glyphs, cursor->next_glyph++);
    cursor->glyph_width = glyph.width;
    cursor->glyph_height = glyph.height;
    cursor->pos = ((uint32_t)glyph.y + glyph.height - 1) * t->width + (uint32_t)glyph.x;
}

static inline void gmd2_next_glyph_row(const Gmd2Traversal *t, Gmd2Cursor *cursor) {
    if (++cursor->y == cursor->glyph_height)
        gmd2_next_glyph(t, cursor);
    else
        cursor->pos -= t->width;
}

static Gmd2Cursor gmd2_start_cursor(const Gmd2Traversal *t) {
    Gmd2Cursor cursor = {0, 0, 0, 0, 0, 0};
    gmd2_next_glyph(t, &cursor);
    return cursor;
}

static inline Gmd2Pixel gmd2_next_outside_pixel(const Gmd2Traversal *t, Gmd2Cursor *cursor) {
    uint32_t w = t->width;
    Gmd2Pixel pixel;
    uint32_t pos = cursor->pos;
    while (t->covered[pos / 8] & (1u << (pos % 8)))
        pos++;
    cursor->pos = pos + 1;
    uint32_t x = pos % w;
    int has_above = pos >= w;
    pixel.pos = pos;
    pixel.left = x ? pos - 1 : UINT32_MAX;
    pixel.above = has_above ? pos - w : UINT32_MAX;
    pixel.upper_left = x && has_above ? pos - w - 1 : UINT32_MAX;
    pixel.above2 = pos / 2 >= w ? pos - 2*w : UINT32_MAX;
    return pixel;
}

static inline uint8_t gmd2_paeth(int a, int b, int c) {
    int low = a < b ? a : b;
    int high = a < b ? b : a;
    int threshold = 3 * c - (a + b);
    /* At equality Paeth prefers an endpoint over the diagonal. */
    return (uint8_t)(threshold <= low ? high : threshold >= high ? low : c);
}

static inline uint8_t gmd2_rgb_median(const uint8_t *px) {
    uint8_t r = px[0], g = px[1], b = px[2];
    uint8_t low = r < g ? r : g;
    uint8_t high = r < g ? g : r;
    return b < low ? low : b > high ? high : b;
}

static inline int gmd2_shared_channel(int mapid,int mk) {
    switch(mk) {
        case 0:return mapid/8;
        case 1:return 1+(mapid%8)/4;
        case 2:return ((mapid%4)/2)*2;
        case 3:return mapid%2;
        default:return 1;
    }
}
#endif

enum { GMD2_ENCODE_MAP = 18 };

/* Glyph rows are visited bottom-up. */
static inline Gmd2Pixel gmd2_glyph_pixel(uint32_t w, uint32_t pos, uint32_t x, uint32_t y) {
    Gmd2Pixel pixel;
    pixel.pos = pos;
    pixel.left = x ? pos - 1 : UINT32_MAX;
    pixel.above = y ? pos + w : UINT32_MAX;
    pixel.upper_left = x && y ? pos + w - 1 : UINT32_MAX;
    pixel.above2 = y > 1 ? pos + 2*w : UINT32_MAX;
    return pixel;
}

static inline Gmd2Pixel gmd2_next_pixel(const Gmd2Traversal *t, Gmd2Cursor *cursor) {
    if (!cursor->glyph_width)
        return gmd2_next_outside_pixel(t, cursor);
    uint32_t x = cursor->x;
    Gmd2Pixel pixel = gmd2_glyph_pixel(t->width, cursor->pos + x, x, cursor->y);
    if (++cursor->x == cursor->glyph_width) {
        cursor->x = 0;
        gmd2_next_glyph_row(t, cursor);
    }
    return pixel;
}
static inline int gmd2_neighbor(const uint8_t *pixels, unsigned channels,
                                uint32_t pos, int channel) {
    return pos == UINT32_MAX ? 0 : pixels[channels * (size_t)pos + channel];
}
static void gmd2_store32le(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

static int gmd2_mask_of(const uint8_t *p) {
    int r=p[0],g=p[1],b=p[2];
    if(r==g&&g==b)return 0;
    if(g==b)return 1;
    if(r==b)return 2;
    if(r==g)return 3;
    return 4;
}
static void gmd2_top_palette(const uint32_t counts[256], uint8_t palette[2]) {
    unsigned first = 0, second = 1;
    if (counts[1] > counts[0]) {
        first = 1;
        second = 0;
    }
    for (unsigned x = 2; x < 256; x++) {
        if (counts[x] > counts[first]) {
            second = first;
            first = x;
        } else if (counts[x] > counts[second]) {
            second = x;
        }
    }
    palette[0] = (uint8_t)first;
    palette[1] = (uint8_t)second;
}

static size_t gmd2_raw_bound(uint32_t width, uint32_t height, unsigned channels) {
    if (!channels) channels = 4;
    if (channels != 3 && channels != 4) return 0;
    size_t n = gmd2_pixel_count(width, height);
    if (!n || n > (SIZE_MAX - GMD2_HEADER) / (channels + 1)) return 0;
    return GMD2_HEADER + (channels + 1) * n;
}

size_t gmd2_encode_bound(uint32_t width, uint32_t height, unsigned channels) {
    size_t bound = gmd2_raw_bound(width, height, channels);
    return bound <= INT32_MAX ? bound : 0;
}

size_t gmd2_encode_scratch_size(uint32_t width, uint32_t height, size_t glyph_count) {
    return gmd2_scratch_size(width, height, glyph_count);
}

enum { GMD2_TAIL_SHARED = 1, GMD2_TAIL_EXCEPTION = 2, GMD2_TAIL_ESCAPE = 4 };

/* Each group of 30 symbols ends in the shared-residual mode. */
#define GMD2_FIVE(f) f, f, f, f, f
#define GMD2_TAIL_GROUP(f, escape) \
    f, f, f, f, escape, GMD2_FIVE(f), GMD2_FIVE(f), GMD2_FIVE(f), \
    GMD2_FIVE(f), GMD2_FIVE((f) | GMD2_TAIL_SHARED)
static const uint8_t gmd2_tail_flags[] = {
    GMD2_TAIL_GROUP(0, GMD2_TAIL_ESCAPE),
    GMD2_TAIL_GROUP(0, 0),
    GMD2_TAIL_GROUP(0, 0),
    GMD2_TAIL_GROUP(0, 0),
    GMD2_TAIL_GROUP(0, 0),
    GMD2_TAIL_GROUP(GMD2_TAIL_EXCEPTION, GMD2_TAIL_EXCEPTION)
};
#undef GMD2_TAIL_GROUP
#undef GMD2_FIVE

typedef struct {
    uint8_t *exception, *shared, *escape;
} Gmd2Tail;

static inline void gmd2_write_tail(Gmd2Tail *tail, const uint8_t *pixels, unsigned channels,
                                   uint8_t symbol, unsigned flags, Gmd2Pixel pixel) {
    const uint8_t *px = pixels + channels * (size_t)pixel.pos;
    if (flags == GMD2_TAIL_ESCAPE) {
        memcpy(tail->escape, px, 3);
        tail->escape += 3;
        return;
    }
    int mask = symbol % 5;
    if (flags & GMD2_TAIL_SHARED) {
        int c = gmd2_shared_channel(GMD2_ENCODE_MAP, mask);
        int l = gmd2_neighbor(pixels, channels, pixel.left, c),
            u = gmd2_neighbor(pixels, channels, pixel.above, c),
            ul = gmd2_neighbor(pixels, channels, pixel.upper_left, c);
        *tail->shared++ = (uint8_t)(px[c] - gmd2_paeth(l, u, ul));
    }
    if (flags & GMD2_TAIL_EXCEPTION) {
        int c = mask - 1;
        int l = gmd2_neighbor(pixels, channels, pixel.left, c),
            u = gmd2_neighbor(pixels, channels, pixel.above, c),
            ul = gmd2_neighbor(pixels, channels, pixel.upper_left, c);
        *tail->exception++ = (uint8_t)(px[c] - (l + u - ul));
    }
}

static void gmd2_replay_symbols(const Gmd2Traversal *t, const uint8_t *pixels, unsigned channels,
                                const uint8_t *symbols, size_t count, Gmd2Tail tail) {
    Gmd2Cursor cursor = gmd2_start_cursor(t);
    size_t i = 0;
    while (cursor.glyph_width) {
        for (uint32_t x = 0; x < cursor.glyph_width; x++) {
            uint8_t symbol = symbols[i++];
            unsigned flags = gmd2_tail_flags[symbol];
            if (flags) {
                Gmd2Pixel pixel = gmd2_glyph_pixel(t->width, cursor.pos + x, x, cursor.y);
                gmd2_write_tail(&tail, pixels, channels, symbol, flags, pixel);
            }
        }
        gmd2_next_glyph_row(t, &cursor);
    }
    for (; i < count; i++) {
        Gmd2Pixel pixel = gmd2_next_outside_pixel(t, &cursor);
        uint8_t symbol = symbols[i];
        unsigned flags = gmd2_tail_flags[symbol];
        if (flags)
            gmd2_write_tail(&tail, pixels, channels, symbol, flags, pixel);
    }
}

static void gmd2_write_alpha(const Gmd2Traversal *t, const uint8_t *pixels,
                             size_t n, uint8_t *alpha) {
    Gmd2Cursor cursor = gmd2_start_cursor(t);
    size_t i = 0;
    while (cursor.glyph_width) {
        const uint8_t *px = pixels + 4 * (size_t)cursor.pos;
        for (uint32_t x = 0; x < cursor.glyph_width; x++, px += 4)
            alpha[i++] = (uint8_t)(px[3] - gmd2_rgb_median(px));
        gmd2_next_glyph_row(t, &cursor);
    }
    for (; i < n; i++) {
        Gmd2Pixel pixel = gmd2_next_outside_pixel(t, &cursor);
        const uint8_t *px = pixels + 4 * (size_t)pixel.pos;
        alpha[i] = (uint8_t)(px[3] - gmd2_rgb_median(px));
    }
}

static int32_t gmd2_encode_pixels(const uint8_t *pixels, size_t pixels_len,
                             uint32_t width, uint32_t height,
                             Gmd2GlyphInput glyphs, unsigned channels,
                             void *scratch, size_t scratch_len,
                             uint8_t *out, size_t capacity) {
    if (!pixels || !out || (channels && channels != 3 && channels != 4))
        return GMD2_ERR_INVALID_ARGUMENT;
    size_t n = gmd2_pixel_count(width, height);
    if (!channels) channels = pixels_len == n * 4 ? 4 : 3;
    if (!gmd2_encode_bound(width, height, channels))
        return GMD2_ERR_INVALID_LAYOUT;
    if (pixels_len != n * channels)
        return GMD2_ERR_INVALID_ARGUMENT;
    if (capacity < GMD2_HEADER + n)
        return GMD2_ERR_OUTPUT_TOO_SMALL;

    Gmd2Traversal t;
    int status = gmd2_setup_traversal(width, height, glyphs, scratch, scratch_len, &t);
    if (status != GMD2_OK) return status;
    uint8_t *body=out+GMD2_HEADER;
    int has_alpha=0;
    uint32_t shist[256]={0},ehist[256]={0};
    for(uint32_t i=0;i<n;i++) {
        const uint8_t *px=pixels+channels*(size_t)i;
        if(channels==4 && px[3]!=255)has_alpha=1;
        int mk=gmd2_mask_of(px);
        if(mk!=4)shist[mk==1?px[1]:px[0]]++;
        if(mk>0&&mk<4)ehist[px[mk-1]]++;
    }
    uint8_t bp[2], ep[2];
    gmd2_top_palette(shist, bp);
    gmd2_top_palette(ehist, ep);
    size_t ne=0,ns=0,nx=0;
    Gmd2Cursor cursor=gmd2_start_cursor(&t);
    for(uint32_t i=0;i<n;i++) {
        Gmd2Pixel pixel=gmd2_next_pixel(&t,&cursor);
        const uint8_t *px=pixels+channels*(size_t)pixel.pos;
        int mk=gmd2_mask_of(px);
        if(mk==4) {
            body[i]=GMD2_ESCAPE_SYMBOL;nx++;continue;
        }
        int v=px[mk==1?1:0];
        int sm;
        if(v==bp[0])sm=0;
        else if(v==bp[1])sm=1;
        else {
            int c=gmd2_shared_channel(GMD2_ENCODE_MAP,mk);
            int l=gmd2_neighbor(pixels,channels,pixel.left,c),
                u=gmd2_neighbor(pixels,channels,pixel.above,c),
                ul=gmd2_neighbor(pixels,channels,pixel.upper_left,c);
            if(v==gmd2_paeth(l,u,ul))sm=2;
            else if(v==l)sm=3;
            else if(v==u)sm=4;
            else {sm=5;ns++;}
        }
        int em=0;
        if(mk>0) {
            int c=mk-1;
            v=px[c];
            if(v==ep[0])em=0;
            else if(v==ep[1])em=1;
            else {
                int l=gmd2_neighbor(pixels,channels,pixel.left,c),
                    u=gmd2_neighbor(pixels,channels,pixel.above,c),
                    ul=gmd2_neighbor(pixels,channels,pixel.upper_left,c);
                if(v==(uint8_t)(l+u-ul))em=2;
                else if(v==l)em=3;
                else if(v==(uint8_t)(2*u-gmd2_neighbor(pixels,channels,pixel.above2,c)))em=4;
                else {em=5;ne++;}
            }
        }
        body[i]=(uint8_t)(mk+5*sm+30*em);
    }
    size_t rgb_len=n+ne+ns+nx*3;
    size_t total=GMD2_HEADER+rgb_len+(has_alpha?n:0);
    if (total > capacity) return GMD2_ERR_OUTPUT_TOO_SMALL;

    memcpy(out, "GMD2", 4);
    gmd2_store32le(out + 4, width);
    gmd2_store32le(out + 8, height);
    out[12] = GMD2_ENCODE_MAP;
    out[13] = GMD2_RAW | (has_alpha ? GMD2_ALPHA | GMD2_ALPHA_MEDIAN : 0);
    memcpy(out + 14, bp, sizeof(bp));
    memcpy(out + 16, ep, sizeof(ep));
    if (ne || ns || nx) {
        Gmd2Tail tail = {body + n, body + n + ne, body + n + ne + ns};
        gmd2_replay_symbols(&t, pixels, channels, body, n, tail);
    }
    if (has_alpha) gmd2_write_alpha(&t, pixels, n, body + rgb_len);
    return (int32_t)total;
}

int32_t gmd2_encode(const uint8_t *pixels, size_t pixels_len, const Gmd2AtlasView *atlas,
                    unsigned channels, uint8_t *out, size_t capacity,
                    void *scratch, size_t scratch_len) {
    if (!atlas) return GMD2_ERR_INVALID_ARGUMENT;
    Gmd2GlyphInput input = {atlas->glyphs, atlas->glyph_count, GMD2_JSON_GLYPHS, atlas->y_origin, 0};
    return gmd2_encode_pixels(pixels, pixels_len, atlas->width, atlas->height, input, channels,
                              scratch, scratch_len, out, capacity);
}

int32_t gmd2_encode_artery(const uint8_t *pixels, size_t pixels_len, const Gmd2ArteryAtlasView *atlas,
                           unsigned channels, uint8_t *out, size_t capacity,
                           void *scratch, size_t scratch_len) {
    if (!atlas) return GMD2_ERR_INVALID_ARGUMENT;
    Gmd2GlyphInput input = {atlas->glyphs, atlas->glyph_count, GMD2_ARTERY_GLYPHS, GMD2_ORIGIN_BOTTOM, atlas->image_index};
    return gmd2_encode_pixels(pixels, pixels_len, atlas->width, atlas->height, input, channels,
                              scratch, scratch_len, out, capacity);
}

#endif
#endif
