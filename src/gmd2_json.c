#include "gmd2.h"

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *begin, *end; } Json;

static void space(Json *input) {
    while (input->begin < input->end && *input->begin &&
           strchr(" \t\r\n", *input->begin))
        input->begin++;
}

static int take(Json *input, char ch) {
    space(input);
    if (input->begin == input->end || *input->begin != ch) return 0;
    input->begin++;
    return 1;
}

static int digit(Json input) {
    return input.begin < input.end && *input.begin >= '0' && *input.begin <= '9';
}

static int number_end(Json *input) {
    if (input->begin < input->end && *input->begin == '-') input->begin++;
    if (!digit(*input)) return 0;
    if (*input->begin++ != '0')
        while (digit(*input)) input->begin++;
    if (input->begin < input->end && *input->begin == '.') {
        input->begin++;
        if (!digit(*input)) return 0;
        while (digit(*input)) input->begin++;
    }
    if (input->begin < input->end && (*input->begin == 'e' || *input->begin == 'E')) {
        input->begin++;
        if (input->begin < input->end && (*input->begin == '+' || *input->begin == '-'))
            input->begin++;
        if (!digit(*input)) return 0;
        while (digit(*input)) input->begin++;
    }
    return 1;
}

static int string_end(Json *input) {
    if (!take(input, '"')) return 0;
    while (input->begin < input->end) {
        unsigned char ch = (unsigned char)*input->begin++;
        if (ch == '"') return 1;
        if (ch < 32) return 0;
        if (ch == '\\') {
            if (input->begin == input->end) return 0;
            ch = (unsigned char)*input->begin++;
            if (ch == 'u') {
                for (int i = 0; i < 4; i++) {
                    if (input->begin == input->end || !*input->begin ||
                        !strchr("0123456789abcdefABCDEF", *input->begin++)) return 0;
                }
            } else if (!ch || !strchr("\"\\/bfnrt", ch)) return 0;
        }
    }
    return 0;
}

static int literal(Json *input, const char *text) {
    size_t len = strlen(text);
    if ((size_t)(input->end - input->begin) < len || memcmp(input->begin, text, len))
        return 0;
    input->begin += len;
    return 1;
}

static int value(Json *input, Json *token, unsigned depth) {
    space(input);
    if (depth > 64 || input->begin == input->end) return 0;
    token->begin = input->begin;
    char ch = *input->begin;
    if (ch == '{' || ch == '[') {
        char close = ch == '{' ? '}' : ']';
        input->begin++;
        if (!take(input, close)) {
            do {
                Json ignored;
                if (ch == '{' && (!string_end(input) || !take(input, ':'))) return 0;
                if (!value(input, &ignored, depth+1)) return 0;
            } while (take(input, ','));
            if (!take(input, close)) return 0;
        }
    } else if (ch == '"') {
        if (!string_end(input)) return 0;
    } else if (ch == 't' || ch == 'f' || ch == 'n') {
        if (!literal(input, ch == 't' ? "true" : ch == 'f' ? "false" : "null")) return 0;
    } else if (!number_end(input)) return 0;
    token->end = input->begin;
    return 1;
}

static int string_is(Json token, const char *text) {
    size_t len = strlen(text);
    return token.begin && (size_t)(token.end-token.begin) == len+2 && *token.begin == '"' &&
           !memcmp(token.begin+1, text, len);
}

static Json field(Json object, const char *name) {
    if (object.begin && take(&object, '{') && !take(&object, '}')) {
        do {
            space(&object);
            Json key = {object.begin, NULL}, token;
            if (!string_end(&object)) break;
            key.end = object.begin;
            if (!take(&object, ':') || !value(&object, &token, 0)) break;
            if (string_is(key, name)) return token;
        } while (take(&object, ','));
    }
    Json missing = {NULL, NULL};
    return missing;
}

static int read_number(Json token, double *out) {
    if (!token.begin || (*token.begin != '-' && !digit(token))) return 0;
    char local[64];
    const char *point = localeconv()->decimal_point;
    size_t point_len = strlen(point);
    size_t len = (size_t)(token.end-token.begin);
    if (len >= sizeof(local) || point_len >= sizeof(local)-len) return 0;
    size_t i = 0;
    for (const char *p = token.begin; p != token.end; p++) {
        if (*p == '.') {
            memcpy(local+i, point, point_len);
            i += point_len;
        } else local[i++] = *p;
    }
    local[i] = 0;
    char *end;
    *out = strtod(local, &end);
    return !*end && isfinite(*out);
}

static int read_uint(Json token, uint32_t *out) {
    double n;
    if (!read_number(token, &n) || n < 0 || n > UINT32_MAX || floor(n) != n)
        return 0;
    *out = (uint32_t)n;
    return 1;
}

static int read_glyph(Json glyph, int top_origin, uint32_t height, Gmd2GlyphRect *out) {
    Json id = field(glyph, "index");
    if (!id.begin) id = field(glyph, "unicode");
    if (!read_uint(id, &out->index)) return 0;
    Json bounds = field(glyph, "atlasBounds");
    if (!bounds.begin) return 1;
    double left, right, bottom, top;
    if (!read_number(field(bounds, "left"), &left) ||
        !read_number(field(bounds, "right"), &right) ||
        !read_number(field(bounds, "bottom"), &bottom) ||
        !read_number(field(bounds, "top"), &top)) return 0;
    double x = floor(left);
    double y = top_origin ? floor(top) : height-ceil(top);
    double w = nearbyint(right-left);
    double h = nearbyint(top_origin ? bottom-top : top-bottom);
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX ||
        w > UINT32_MAX || h > UINT32_MAX || w < 0 || h < 0) return 0;
    out->x = (int32_t)x;
    out->y = (int32_t)y;
    out->width = (uint32_t)w;
    out->height = (uint32_t)h;
    return 1;
}

static int read_glyphs(Json array, int top_origin, Gmd2Metadata *layout, size_t *capacity) {
    if (!array.begin || !take(&array, '[')) return 0;
    if (take(&array, ']')) return 1;
    do {
        Json token;
        Gmd2GlyphRect glyph = {0};
        if (!value(&array, &token, 0) ||
            !read_glyph(token, top_origin, layout->height, &glyph)) return 0;
        size_t count = layout->glyph_count;
        if (count == UINT32_MAX-1) return 0;
        if (count == *capacity) {
            if (count > SIZE_MAX / sizeof(glyph) / 2) return 0;
            size_t next = count ? 2*count : 16;
            Gmd2GlyphRect *items = realloc(layout->glyphs, next*sizeof(glyph));
            if (!items) return 0;
            layout->glyphs = items;
            *capacity = next;
        }
        glyph.source_order = (uint32_t)count;
        layout->glyphs[layout->glyph_count++] = glyph;
    } while (take(&array, ','));
    return take(&array, ']');
}

int gmd2_layout_from_msdf_json(const char *data, size_t len, Gmd2Metadata *layout,
                               char *error, size_t error_len) {
    if (!layout) {
        if (error && error_len) snprintf(error, error_len, "missing layout output");
        return -1;
    }
    memset(layout, 0, sizeof(*layout));
    if (!data) goto bad;
    Json input = {data, data+len}, root;
    if (!value(&input, &root, 0)) goto bad;
    space(&input);
    if (input.begin != input.end) goto bad;
    Json atlas = field(root, "atlas");
    if (!read_uint(field(atlas, "width"), &layout->width) ||
        !read_uint(field(atlas, "height"), &layout->height) ||
        !layout->width || !layout->height ||
        (uint64_t)layout->width*layout->height > UINT32_MAX) goto bad;
    Json origin = field(atlas, "yOrigin");
    int top_origin = string_is(origin, "top");
    if (!top_origin && !string_is(origin, "bottom")) goto bad;

    size_t capacity = 0;
    Json variants = field(root, "variants");
    if (variants.begin) {
        if (!take(&variants, '[')) goto bad;
        do {
            Json variant;
            if (!value(&variants, &variant, 0) ||
                !read_glyphs(field(variant, "glyphs"), top_origin, layout, &capacity))
                goto bad;
        } while (take(&variants, ','));
        if (!take(&variants, ']')) goto bad;
    } else if (!read_glyphs(field(root, "glyphs"), top_origin, layout, &capacity)) goto bad;
    return 0;

bad:
    gmd2_free_layout(layout);
    if (error && error_len) snprintf(error, error_len, "invalid msdf-atlas-gen JSON layout");
    return -1;
}
