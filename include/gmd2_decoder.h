/* Generated from src/gmd2_codec.in. */
#ifndef GMD2_DECODER_H
#define GMD2_DECODER_H
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

/* Define GMD2_DECODER_IMPLEMENTATION before including this header in one translation unit.
 * Use disjoint buffers and the same dimensions and glyphs as the encoder.
 * Pass channels 0 to infer RGB8 or RGBA8. With channels 4, missing alpha is 255.
 * Decoding returns GMD2_OK or a negative GMD2_ERR_* code. */
/* Output bytes from the stream header, or 0 on error. */
size_t gmd2_decode_size(const uint8_t *data, size_t length, unsigned channels);
/* Required scratch bytes, or 0 on error. Scratch can have any alignment. */
size_t gmd2_decode_scratch_size(uint32_t width, uint32_t height, size_t glyph_count);
int gmd2_decode(const uint8_t *data, size_t length, const Gmd2AtlasView *atlas,
                unsigned channels, uint8_t *out, size_t capacity,
                void *scratch, size_t scratch_len);
int gmd2_decode_artery(const uint8_t *data, size_t length, const Gmd2ArteryAtlasView *atlas,
                       unsigned channels, uint8_t *out, size_t capacity,
                       void *scratch, size_t scratch_len);

#ifdef __cplusplus
}
#endif

#ifdef GMD2_DECODER_IMPLEMENTATION
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

enum { GMD2_ESCAPE_MODE = 254, GMD2_INVALID_MODE = 255 };

/* Each descriptor packs a 2-bit mask and two 3-bit predictor modes. */
static void gmd2_unpack_modes(uint8_t modes[256]) {
    memset(modes, GMD2_INVALID_MODE, 256);
    for (unsigned em = 0; em < 6; em++) {
        for (unsigned sm = 0; sm < 6; sm++) {
            for (unsigned mk = em != 0; mk < 4; mk++) {
                unsigned symbol = mk + 5 * sm + 30 * em;
                modes[symbol] = (uint8_t)(mk | sm << 2 | em << 5);
            }
        }
    }
    modes[GMD2_ESCAPE_SYMBOL] = GMD2_ESCAPE_MODE;
}

static uint32_t gmd2_load32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

static unsigned gmd2_raw_flags(const uint8_t *data) {
    unsigned flags = data[13];
    if (memcmp(data, "GMD2", 4) || data[12] >= 24 ||
        (flags != GMD2_RAW && flags != (GMD2_RAW | GMD2_ALPHA) &&
         flags != (GMD2_RAW | GMD2_ALPHA | GMD2_ALPHA_MEDIAN)))
        return 0;
    return flags;
}

static unsigned gmd2_output_channels(unsigned flags, unsigned channels) {
    if (!channels) return (flags & GMD2_ALPHA) ? 4 : 3;
    if (channels == 4 || (channels == 3 && !(flags & GMD2_ALPHA))) return channels;
    return 0;
}

size_t gmd2_decode_size(const uint8_t *data, size_t length, unsigned channels) {
    if (!data || length < GMD2_HEADER) return 0;
    unsigned flags = gmd2_raw_flags(data);
    if (!flags) return 0;
    size_t count = gmd2_pixel_count(gmd2_load32le(data + 4), gmd2_load32le(data + 8));
    return count * gmd2_output_channels(flags, channels);
}

size_t gmd2_decode_scratch_size(uint32_t width, uint32_t height, size_t glyph_count) {
    return gmd2_scratch_size(width, height, glyph_count);
}

static int gmd2_decode_pixels(const uint8_t *data, size_t length,
                             uint32_t width, uint32_t height,
                             Gmd2GlyphInput glyphs, unsigned channels,
                             void *scratch, size_t scratch_len,
                             uint8_t *out, size_t capacity) {
    if (!data || !out || (channels && channels != 3 && channels != 4))
        return GMD2_ERR_INVALID_ARGUMENT;
    size_t n = gmd2_pixel_count(width, height);
    if (!n)
        return GMD2_ERR_INVALID_LAYOUT;
    if (length < GMD2_HEADER)
        return GMD2_ERR_INVALID_STREAM;
    unsigned flags=gmd2_raw_flags(data);
    if(!flags)
        return GMD2_ERR_INVALID_STREAM;
    if(gmd2_load32le(data+4)!=width||gmd2_load32le(data+8)!=height)
        return GMD2_ERR_INVALID_LAYOUT;
    size_t alpha_len=(flags&GMD2_ALPHA)?n:0;
    channels=gmd2_output_channels(flags,channels);
    if(!channels)
        return GMD2_ERR_CHANNEL_MISMATCH;
    if(capacity<n*channels)
        return GMD2_ERR_OUTPUT_TOO_SMALL;

    const uint8_t *body=data+GMD2_HEADER;
    size_t body_size=length-GMD2_HEADER;
    if(body_size<alpha_len || body_size-alpha_len<n)
        return GMD2_ERR_INVALID_STREAM;
    size_t rgb_len=body_size-alpha_len;
    uint8_t modes[256];
    gmd2_unpack_modes(modes);
    uint32_t increments[256];
    for (unsigned i = 0; i < 256; i++) {
        unsigned mode = modes[i];
        increments[i] = (((mode >> 2) & 7) == 5) |
                        (uint32_t)((mode >> 5) == 5) << 8 |
                        (uint32_t)(mode == GMD2_ESCAPE_MODE) << 16 |
                        (uint32_t)(mode == GMD2_INVALID_MODE) << 24;
    }
    size_t nshared=0,nexception=0,nesc=0;
    /* Limit each sum to 128 symbols so byte counts cannot carry. */
    for (size_t i = 0; i < n;) {
        size_t end = i + (n - i < 128 ? n - i : 128);
        uint32_t sum = 0;
        for (; i < end; i++)
            sum += increments[body[i]];
        if (sum >> 24) return GMD2_ERR_INVALID_STREAM;
        nshared += sum & 255;
        nexception += (sum >> 8) & 255;
        nesc += (sum >> 16) & 255;
    }
    if(rgb_len!=n+nexception+nshared+nesc*3)
        return GMD2_ERR_INVALID_STREAM;

    Gmd2Traversal t;
    int status = gmd2_setup_traversal(width, height, glyphs, scratch, scratch_len, &t);
    if (status != GMD2_OK) return status;
    /* Start residual cursors one byte early so masked reads stay in bounds. */
    const uint8_t *er=body+n-1, *sr=er+nexception, *esc=sr+nshared+1;
    const uint8_t *alpha=body+rgb_len;
    const int predicted_alpha = flags & GMD2_ALPHA_MEDIAN;
    if (channels == 4 && !predicted_alpha) {
        if (alpha_len) {
            for (size_t i = 0; i < n; i++) out[4*i+3] = alpha[i];
        } else {
            memset(out, 255, 4 * n);
        }
    }
    uint8_t shared_channels[4];
    for (int mk=0; mk<4; mk++)
        shared_channels[mk]=(uint8_t)gmd2_shared_channel(data[12],mk);
    static const uint8_t zero[3] = {0, 0, 0};
    size_t stride = channels * (size_t)width;
    Gmd2Cursor cursor = gmd2_start_cursor(&t);
    for (size_t i = 0; i < n;) {
        uint32_t pos, span;
        const uint8_t *left, *upper_left, *prev, *prev2;
        if (cursor.glyph_width) {
            pos = cursor.pos;
            span = cursor.glyph_width;
            const uint8_t *row = out + channels * (size_t)pos;
            left = upper_left = zero;
            prev = cursor.y ? row + stride : NULL;
            prev2 = cursor.y > 1 ? row + 2 * stride : NULL;
            gmd2_next_glyph_row(&t, &cursor);
        } else {
            Gmd2Pixel pixel = gmd2_next_outside_pixel(&t, &cursor);
            pos = pixel.pos;
            span = 1;
            left = pixel.left == UINT32_MAX ? zero : out + channels * (size_t)pixel.left;
            upper_left = pixel.upper_left == UINT32_MAX ? zero : out + channels * (size_t)pixel.upper_left;
            prev = pixel.above == UINT32_MAX ? NULL : out + channels * (size_t)pixel.above;
            prev2 = pixel.above2 == UINT32_MAX ? NULL : out + channels * (size_t)pixel.above2;
        }
        uint8_t *px = out + channels * (size_t)pos;
        /* Missing predictor rows stay at the zero pixel as the span advances. */
        const uint8_t *above = prev ? prev : zero;
        const uint8_t *above2 = prev2 ? prev2 : zero;
        unsigned above_step = prev ? channels : 0;
        unsigned above2_step = prev2 ? channels : 0;
        for (uint32_t x = 0; x < span; x++, px += channels) {
            unsigned mode = modes[body[i++]];
            uint8_t median;
            if(mode==GMD2_ESCAPE_MODE) {
                memcpy(px,esc,3);
                esc+=3;
                median = predicted_alpha ? gmd2_rgb_median(px) : 0;
            } else {
                int mk=mode&3,sm=(mode>>2)&7,em=mode>>5;
                int c=shared_channels[mk];
                uint8_t v;
                if(sm<2)v=data[14+sm];
                else if(sm==3)v=(uint8_t)left[c];
                else if(sm==4)v=(uint8_t)above[c];
                else {
                    int l=left[c],
                        u=above[c],
                        ul=upper_left[c];
                    unsigned residual = sm == 5;
                    sr += residual;
                    v = (uint8_t)(gmd2_paeth(l,u,ul) + (*sr & (0u - residual)));
                }
                px[0]=px[1]=px[2]=v;
                /* The shared value occurs at least twice in RGB. */
                median = v;
                if(mk>0) {
                    c=mk-1;
                    if(em<2)v=data[16+em];
                    else if(em==3)v=(uint8_t)left[c];
                    else if(em==4) {
                        int u=above[c],
                            uu=above2[c];
                        v=(uint8_t)(2*u-uu);
                    } else {
                        int l=left[c],
                            u=above[c],
                            ul=upper_left[c];
                        unsigned residual = em == 5;
                        er += residual;
                        v = (uint8_t)(l+u-ul + (*er & (0u - residual)));
                    }
                    px[c]=v;
                }
            }
            if (predicted_alpha) px[3] = (uint8_t)(median + *alpha++);
            left = px;
            upper_left = above;
            above += above_step;
            above2 += above2_step;
        }
    }
    return GMD2_OK;
}

int gmd2_decode(const uint8_t *data, size_t length, const Gmd2AtlasView *atlas,
                unsigned channels, uint8_t *out, size_t capacity,
                void *scratch, size_t scratch_len) {
    if (!atlas) return GMD2_ERR_INVALID_ARGUMENT;
    Gmd2GlyphInput input = {atlas->glyphs, atlas->glyph_count, GMD2_JSON_GLYPHS, atlas->y_origin, 0};
    return gmd2_decode_pixels(data, length, atlas->width, atlas->height, input, channels,
                              scratch, scratch_len, out, capacity);
}

int gmd2_decode_artery(const uint8_t *data, size_t length, const Gmd2ArteryAtlasView *atlas,
                       unsigned channels, uint8_t *out, size_t capacity,
                       void *scratch, size_t scratch_len) {
    if (!atlas) return GMD2_ERR_INVALID_ARGUMENT;
    Gmd2GlyphInput input = {atlas->glyphs, atlas->glyph_count, GMD2_ARTERY_GLYPHS, GMD2_ORIGIN_BOTTOM, atlas->image_index};
    return gmd2_decode_pixels(data, length, atlas->width, atlas->height, input, channels,
                              scratch, scratch_len, out, capacity);
}

#endif
#endif
