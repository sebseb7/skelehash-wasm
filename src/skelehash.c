#include "skelehash.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SH_PI 3.14159265358979323846
#define SH_MAX_MODES 104
static const unsigned band_bits[3] = {6, 4, 3};
static const char alphabet[] = SH_BASE62_ALPHABET;
typedef struct { unsigned x, y, band, bits; double ac; } mode;
typedef struct { mode modes[SH_MAX_MODES]; unsigned count; } channel;
static double clamp(double v, double lo, double hi) { return fmax(lo, fmin(hi, v)); }
/* JS Math.round ties toward +infinity, unlike C round(). */
static int js_round(double v) { return (int)floor(v + 0.5); }
static sh_result *new_result(void) { return (sh_result *)calloc(1, sizeof(sh_result)); }
static sh_result *fail(sh_result *r, sh_status status, const char *message) {
    r->status = status; r->error = message; return r;
}
void sh_result_free(sh_result *r) {
    if (!r) return;
    free(r->base62); free(r->hash); free(r->rgba); free(r->metadata_json); free(r);
}
int sh_result_status(const sh_result *r) { return r ? (int)r->status : SH_OUT_OF_MEMORY; }
const char *sh_result_error(const sh_result *r) { return r ? (r->error ? r->error : "") : "Out of memory"; }
const char *sh_result_base62(const sh_result *r) { return r ? r->base62 : NULL; }
const char *sh_result_metadata_json(const sh_result *r) { return r ? r->metadata_json : NULL; }
const uint8_t *sh_result_hash(const sh_result *r) { return r ? r->hash : NULL; }
const uint8_t *sh_result_rgba(const sh_result *r) { return r ? r->rgba : NULL; }
uint32_t sh_result_hash_length(const sh_result *r) { return r ? r->metadata.hash_bytes : 0; }
uint32_t sh_result_base62_length(const sh_result *r) { return r ? r->metadata.base62_length : 0; }
uint32_t sh_result_rgba_length(const sh_result *r) { return r && r->rgba ? r->metadata.output_rgba_bytes : 0; }
uint32_t sh_result_width(const sh_result *r) { return r ? r->metadata.output_width : 0; }
uint32_t sh_result_height(const sh_result *r) { return r ? r->metadata.output_height : 0; }

static void specs(sh_metadata *m, channel channels[4]) {
    unsigned longest = m->source_width > m->source_height ? m->source_width : m->source_height;
    unsigned limits[4] = {m->has_alpha ? 10u : 14u, 6, 6, 10};
    m->version = 3; m->flags = m->has_alpha; m->channel_count = m->has_alpha ? 4 : 3;
    m->output_width = (unsigned)fmax(1, js_round(64.0 * m->source_width / longest));
    m->output_height = (unsigned)fmax(1, js_round(64.0 * m->source_height / longest));
    m->input_rgba_bytes = m->source_width * m->source_height * 4;
    m->output_rgba_bytes = m->output_width * m->output_height * 4;
    m->aspect_ratio = (double)m->source_width / m->source_height;
    m->hash_bytes = 6;
    for (unsigned c = 0; c < m->channel_count; ++c) {
        sh_channel_metadata *cm = &m->channels[c];
        channel *ch = &channels[c];
        unsigned bits = 0;
        cm->nx = (unsigned)fmax(1, js_round((double)limits[c] * m->source_width / longest));
        cm->ny = (unsigned)fmax(1, js_round((double)limits[c] * m->source_height / longest));
        if (cm->nx > m->source_width) cm->nx = m->source_width;
        if (cm->ny > m->source_height) cm->ny = m->source_height;
        for (unsigned y = 0; y < cm->ny; ++y) {
            for (unsigned x = y ? 0 : 1; x * cm->ny < cm->nx * (cm->ny - y); ++x) {
                unsigned band = x + y <= 2 ? 0 : (double)x / cm->nx + (double)y / cm->ny <= 0.5 ? 1 : 2;
                mode *v = &ch->modes[ch->count++];
                v->x = x; v->y = y; v->band = band; v->bits = band_bits[band];
                cm->band_counts[band]++; bits += v->bits;
            }
        }
        cm->ac_count = ch->count; cm->payload_bytes = 5 + (bits + 7) / 8;
        m->hash_bytes += cm->payload_bytes;
    }
}

/* Base62 is the raw hash interpreted as one unsigned big-endian integer.
 * TH03 starts with nonzero 0x54, so there is no leading-zero ambiguity.
 * Reject leading zero digits; no prefixes, whitespace, padding, or separators.
 */
static char *to_base62(const uint8_t *bytes, size_t n) {
    uint8_t work[SH_MAX_HASH_BYTES]; char reversed[SH_MAX_BASE62_LENGTH];
    size_t start = 0, length = 0;
    memcpy(work, bytes, n);
    while (start < n) {
        unsigned remainder = 0;
        for (size_t i = start; i < n; ++i) {
            unsigned value = remainder * 256 + work[i];
            work[i] = (uint8_t)(value / 62); remainder = value % 62;
        }
        reversed[length++] = alphabet[remainder];
        while (start < n && work[start] == 0) ++start;
    }
    char *text = (char *)malloc(length + 1);
    if (!text) return NULL;
    for (size_t i = 0; i < length; ++i) text[i] = reversed[length - 1 - i];
    text[length] = '\0'; return text;
}
static int from_base62(const char *text, size_t n, uint8_t bytes[SH_MAX_HASH_BYTES], size_t *length) {
    uint8_t little[SH_MAX_HASH_BYTES] = {0}; size_t used = 1;
    if (!n || n > SH_MAX_BASE62_LENGTH || text[0] == '0') return 0;
    for (size_t i = 0; i < n; ++i) {
        const char *p = text[i] ? strchr(alphabet, (unsigned char)text[i]) : NULL;
        if (!p) return 0;
        unsigned carry = (unsigned)(p - alphabet);
        for (size_t j = 0; j < used; ++j) {
            unsigned value = little[j] * 62u + carry;
            little[j] = (uint8_t)value; carry = value >> 8;
        }
        while (carry) {
            if (used == SH_MAX_HASH_BYTES) return 0;
            little[used++] = (uint8_t)carry; carry >>= 8;
        }
    }
    for (size_t i = 0; i < used; ++i) bytes[i] = little[used - 1 - i];
    *length = used; return 1;
}

/* Tables are heap allocated; no huge per-call stack buffers for WASM. */
static double *cosines(unsigned size, unsigned count) {
    double *table = (double *)malloc((size_t)size * count * sizeof(double));
    if (!table) return NULL;
    for (unsigned k = 0; k < count; ++k)
        for (unsigned x = 0; x < size; ++x)
            table[k * size + x] = cos(SH_PI / size * k * (x + 0.5));
    return table;
}
static double pixel_channel(const uint8_t *rgba, size_t i, unsigned c) {
    double a = rgba[i * 4 + 3] / 255.0;
    double r = rgba[i * 4] / 255.0 * a, g = rgba[i * 4 + 1] / 255.0 * a, b = rgba[i * 4 + 2] / 255.0 * a;
    if (c == 0) return (r + g + b) / 3;
    if (c == 1) return (r + g) / 2 - b;
    if (c == 2) return r - g;
    return a;
}
static int encode_channel(const uint8_t *rgba, unsigned w, unsigned h, unsigned c,
                           sh_channel_metadata *cm, channel *ch, uint8_t *out) {
    double *fx = cosines(w, cm->nx), *fy = cosines(h, cm->ny);
    double *horizontal = (double *)calloc((size_t)h * cm->nx, sizeof(double));
    double *values = (double *)malloc((size_t)w * h * sizeof(double));
    if (!fx || !fy || !horizontal || !values) {
        free(fx); free(fy); free(horizontal); free(values); return 0;
    }
    double dc = 0, scales[3] = {0};
    for (size_t i = 0; i < (size_t)w * h; ++i) { values[i] = pixel_channel(rgba, i, c); dc += values[i]; }
    dc /= (double)w * h;
    for (unsigned y = 0; y < h; ++y)
        for (unsigned xmode = 0; xmode < cm->nx; ++xmode)
            for (unsigned x = 0; x < w; ++x)
                horizontal[y * cm->nx + xmode] += values[y * w + x] * fx[xmode * w + x];
    for (unsigned i = 0; i < ch->count; ++i) {
        mode *v = &ch->modes[i]; double sum = 0;
        for (unsigned y = 0; y < h; ++y) sum += horizontal[y * cm->nx + v->x] * fy[v->y * h + y];
        v->ac = sum / ((double)w * h);
        scales[v->band] = fmax(scales[v->band], fabs(v->ac));
    }
    free(fx); free(fy); free(horizontal); free(values);
    int is_signed = c == 1 || c == 2;
    cm->dc_code = (unsigned)js_round(clamp(is_signed ? (dc + 1) / 2 : dc, 0, 1) * 65535);
    cm->dc = cm->dc_code / 65535.0;
    if (is_signed) cm->dc = 2 * cm->dc - 1;
    out[0] = (uint8_t)cm->dc_code; out[1] = (uint8_t)(cm->dc_code >> 8);
    for (unsigned b = 0; b < 3; ++b) {
        cm->scale_codes[b] = scales[b] < 1e-12 ? 0 : (unsigned)ceil(255 * sqrt(clamp(scales[b], 0, 1)));
        double s = cm->scale_codes[b] / 255.0; cm->scales[b] = s * s;
        out[2 + b] = (uint8_t)cm->scale_codes[b];
    }
    unsigned accumulator = 0, available = 0; size_t offset = 5;
    for (unsigned i = 0; i < ch->count; ++i) {
        mode *v = &ch->modes[i]; int half = (1 << (v->bits - 1)) - 1;
        double scale = cm->scales[v->band];
        unsigned code = (unsigned)(half + (scale ? js_round(clamp(v->ac / scale, -1, 1) * half) : 0));
        accumulator |= code << available; available += v->bits;
        while (available >= 8) { out[offset++] = (uint8_t)accumulator; accumulator >>= 8; available -= 8; }
    }
    if (available) out[offset] = (uint8_t)accumulator;
    return 1;
}
static int decode_channel(const uint8_t *in, unsigned c, sh_channel_metadata *cm, channel *ch) {
    cm->dc_code = in[0] | (unsigned)in[1] << 8;
    cm->dc = cm->dc_code / 65535.0;
    if (c == 1 || c == 2) cm->dc = 2 * cm->dc - 1;
    for (unsigned b = 0; b < 3; ++b) {
        cm->scale_codes[b] = in[2 + b]; double s = in[2 + b] / 255.0; cm->scales[b] = s * s;
    }
    unsigned accumulator = 0, available = 0; size_t offset = 5;
    for (unsigned i = 0; i < ch->count; ++i) {
        mode *v = &ch->modes[i];
        while (available < v->bits) { accumulator |= (unsigned)in[offset++] << available; available += 8; }
        unsigned mask = (1u << v->bits) - 1, code = accumulator & mask;
        accumulator >>= v->bits; available -= v->bits;
        if (code == mask) return 0;
        int half = (1 << (v->bits - 1)) - 1;
        v->ac = ((int)code - half) / (double)half * cm->scales[v->band];
    }
    return accumulator == 0;
}
/* Lanczos sigma factor: tapers high-frequency modes to suppress the Gibbs ringing
 * (dotted/blocky artefacts) of the truncated DCT. Decoder-only; hash format unchanged. */
static double sinc_taper(unsigned k, unsigned n) {
    if (!k) return 1.0;
    double t = SH_PI * (double)k / (double)n;
    return sin(t) / t;
}
static double render(const sh_channel_metadata *cm, const channel *ch,
                     const double *fx, const double *fy, unsigned w, unsigned h, unsigned x, unsigned y) {
    double value = cm->dc;
    for (unsigned i = 0; i < ch->count; ++i) {
        const mode *v = &ch->modes[i];
        double taper = sinc_taper(v->x, cm->nx) * sinc_taper(v->y, cm->ny);
        value += v->ac * taper * fx[v->x * w + x] * fy[v->y * h + y] * (v->x ? 2 : 1) * (v->y ? 2 : 1);
    }
    return value;
}
static int metadata_json(sh_result *r) {
    sh_metadata *m = &r->metadata; size_t used = 0; const size_t capacity = 4096;
    r->metadata_json = (char *)malloc(capacity);
    if (!r->metadata_json) return 0;
    /* snprintf lengths checked before advancing the cursor. */
#define APPEND(...) do { int n = snprintf(r->metadata_json + used, capacity - used, __VA_ARGS__); \
    if (n < 0 || (size_t)n >= capacity - used) { return 0; } used += (size_t)n; } while (0)
    APPEND("{\"version\":%u,\"sourceWidth\":%u,\"sourceHeight\":%u,\"outputWidth\":%u,\"outputHeight\":%u,\"hasAlpha\":%s,\"flags\":%u,\"channelCount\":%u,\"hashBytes\":%u,\"base62Length\":%u,\"inputRgbaBytes\":%u,\"outputRgbaBytes\":%u,\"aspectRatio\":%.17g,\"base62Alphabet\":\"%s\",\"bandBits\":[6,4,3],\"channels\":[",
        m->version, m->source_width, m->source_height, m->output_width, m->output_height,
        m->has_alpha ? "true" : "false", m->flags, m->channel_count, m->hash_bytes,
        m->base62_length, m->input_rgba_bytes, m->output_rgba_bytes, m->aspect_ratio, alphabet);
    const char *names[4] = {"L", "P", "Q", "A"};
    for (unsigned i = 0; i < m->channel_count; ++i) {
        sh_channel_metadata *c = &m->channels[i];
        APPEND("%s{\"name\":\"%s\",\"nx\":%u,\"ny\":%u,\"acCount\":%u,\"payloadBytes\":%u,\"bandCounts\":[%u,%u,%u],\"dcCode\":%u,\"dc\":%.17g,\"scaleCodes\":[%u,%u,%u],\"scales\":[%.17g,%.17g,%.17g]}",
            i ? "," : "", names[i], c->nx, c->ny, c->ac_count, c->payload_bytes,
            c->band_counts[0], c->band_counts[1], c->band_counts[2], c->dc_code, c->dc,
            c->scale_codes[0], c->scale_codes[1], c->scale_codes[2], c->scales[0], c->scales[1], c->scales[2]);
    }
    APPEND("]}");
#undef APPEND
    return 1;
}

sh_result *sh_encode_rgba(uint32_t w, uint32_t h, const uint8_t *rgba, size_t length) {
    sh_result *r = new_result(); if (!r) return NULL;
    if (!rgba || !w || !h || w > 200 || h > 200 || length != (size_t)w * h * 4)
        return fail(r, SH_INVALID_ARGUMENT, "Expected dimensions 1..200 and exactly width*height*4 RGBA bytes");
    sh_metadata *m = &r->metadata; m->source_width = w; m->source_height = h;
    for (size_t i = 3; i < length; i += 4) if (rgba[i] != 255) { m->has_alpha = 1; break; }
    channel channels[4] = {0}; specs(m, channels);
    r->hash = (uint8_t *)calloc(m->hash_bytes, 1);
    if (!r->hash) return fail(r, SH_OUT_OF_MEMORY, "Out of memory");
    r->hash[0] = 0x54; r->hash[1] = 0x48; r->hash[2] = 3;
    r->hash[3] = (uint8_t)w; r->hash[4] = (uint8_t)h; r->hash[5] = (uint8_t)m->flags;
    size_t offset = 6;
    for (unsigned c = 0; c < m->channel_count; ++c) {
        if (!encode_channel(rgba, w, h, c, &m->channels[c], &channels[c], r->hash + offset))
            return fail(r, SH_OUT_OF_MEMORY, "Out of memory");
        offset += m->channels[c].payload_bytes;
    }
    r->base62 = to_base62(r->hash, m->hash_bytes);
    if (!r->base62) return fail(r, SH_OUT_OF_MEMORY, "Out of memory");
    m->base62_length = (uint32_t)strlen(r->base62);
    if (!metadata_json(r)) return fail(r, SH_OUT_OF_MEMORY, "Could not allocate metadata JSON");
    return r;
}
sh_result *sh_decode_base62(const char *text, size_t length) {
    sh_result *r = new_result(); if (!r) return NULL;
    if (!text) return fail(r, SH_INVALID_ARGUMENT, "NULL Base62 input");
    uint8_t bytes[SH_MAX_HASH_BYTES]; size_t n = 0;
    if (!from_base62(text, length, bytes, &n)) return fail(r, SH_INVALID_BASE62, "Invalid, noncanonical, or oversized Base62 string");
    if (n < 3 || bytes[0] != 0x54 || bytes[1] != 0x48) return fail(r, SH_INVALID_HASH, "Invalid SkeleHash signature");
    if (bytes[2] != 3) return fail(r, SH_UNSUPPORTED_VERSION, "Only TH03 is supported");
    if (n < 6 || !bytes[3] || !bytes[4] || bytes[3] > 200 || bytes[4] > 200 || (bytes[5] & ~1u))
        return fail(r, SH_INVALID_HASH, "Invalid SkeleHash header");
    sh_metadata *m = &r->metadata;
    m->source_width = bytes[3]; m->source_height = bytes[4]; m->has_alpha = bytes[5] & 1;
    channel channels[4] = {0}; specs(m, channels);
    if (n != m->hash_bytes) return fail(r, SH_INVALID_HASH, "Invalid SkeleHash payload length");
    size_t offset = 6;
    for (unsigned c = 0; c < m->channel_count; ++c) {
        if (!decode_channel(bytes + offset, c, &m->channels[c], &channels[c]))
            return fail(r, SH_INVALID_HASH, "Reserved coefficient code or nonzero padding");
        offset += m->channels[c].payload_bytes;
    }
    r->hash = (uint8_t *)malloc(n); r->base62 = (char *)malloc(length + 1);
    r->rgba = (uint8_t *)calloc(m->output_rgba_bytes, 1);
    if (!r->hash || !r->base62 || !r->rgba) return fail(r, SH_OUT_OF_MEMORY, "Out of memory");
    memcpy(r->hash, bytes, n); memcpy(r->base62, text, length); r->base62[length] = '\0';
    m->base62_length = (uint32_t)length;
    unsigned nx = 0, ny = 0;
    for (unsigned c = 0; c < m->channel_count; ++c) {
        if (m->channels[c].nx > nx) nx = m->channels[c].nx;
        if (m->channels[c].ny > ny) ny = m->channels[c].ny;
    }
    double *fx = cosines(m->output_width, nx), *fy = cosines(m->output_height, ny);
    if (!fx || !fy) { free(fx); free(fy); return fail(r, SH_OUT_OF_MEMORY, "Out of memory"); }
    unsigned w = m->output_width, h = m->output_height;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        double v[4] = {0, 0, 0, 1};
        for (unsigned c = 0; c < m->channel_count; ++c)
            v[c] = render(&m->channels[c], &channels[c], fx, fy, w, h, x, y);
        double a = m->has_alpha ? clamp(v[3], 0, 1) : 1;
        size_t i = ((size_t)y * w + x) * 4;
        if (a > 1.0 / 255) {
            r->rgba[i] = (uint8_t)js_round(255 * clamp(v[0] + v[1] / 3 + v[2] / 2, 0, a) / a);
            r->rgba[i + 1] = (uint8_t)js_round(255 * clamp(v[0] + v[1] / 3 - v[2] / 2, 0, a) / a);
            r->rgba[i + 2] = (uint8_t)js_round(255 * clamp(v[0] - 2.0 / 3 * v[1], 0, a) / a);
        }
        r->rgba[i + 3] = (uint8_t)js_round(255 * a);
    }
    free(fx); free(fy);
    if (!metadata_json(r)) return fail(r, SH_OUT_OF_MEMORY, "Could not allocate metadata JSON");
    return r;
}
