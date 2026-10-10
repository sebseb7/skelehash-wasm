#ifndef SKELEHASH_H
#define SKELEHASH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SH_VERSION 3
#define SH_MAX_INPUT_EDGE 200
#define SH_OUTPUT_EDGE 64
#define SH_MAX_HASH_BYTES 128
#define SH_MAX_BASE62_LENGTH 256
#define SH_BASE62_ALPHABET "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"

typedef enum {
    SH_OK = 0, SH_INVALID_ARGUMENT = 1, SH_OUT_OF_MEMORY = 2,
    SH_INVALID_BASE62 = 3, SH_INVALID_HASH = 4, SH_UNSUPPORTED_VERSION = 5
} sh_status;

typedef struct {
    uint32_t nx, ny, ac_count, payload_bytes;
    uint32_t band_counts[3];
    uint32_t dc_code, scale_codes[3];
    double dc, scales[3]; /* Quantized values actually used by the decoder. */
} sh_channel_metadata;

typedef struct {
    uint32_t version, source_width, source_height, output_width, output_height;
    uint32_t has_alpha, flags, channel_count, hash_bytes, base62_length;
    uint32_t input_rgba_bytes, output_rgba_bytes;
    double aspect_ratio;
    sh_channel_metadata channels[4]; /* L, P, Q, A; unused A is zeroed. */
} sh_metadata;

typedef struct sh_result {
    sh_status status;
    const char *error; /* Static string; do not free. */
    sh_metadata metadata;
    char *base62; /* NUL-terminated. */
    uint8_t *hash; /* Raw TH03 binary, for JS interoperability. */
    uint8_t *rgba; /* Decode only; tightly packed unpremultiplied RGBA8. */
    char *metadata_json; /* Same metadata, accessible without struct offsets. */
} sh_result;

/* Lossy placeholder conversion, NOT original-image recovery.
 * Input buffers are borrowed only during the call, never freed or retained.
 * Return NULL only if result allocation itself fails. Otherwise inspect status.
 * Release every result (successful or failed) with sh_result_free().
 * Encode returns base62 + hash + metadata; rgba is NULL.
 * Decode returns rgba + original base62 + hash + metadata.
 * Metadata describes the encoded/quantized channels, not original pixel statistics.
 */
sh_result *sh_encode_rgba(uint32_t width, uint32_t height,
                          const uint8_t *rgba, size_t rgba_length);
sh_result *sh_decode_base62(const char *text, size_t text_length);
void sh_result_free(sh_result *result);

/* WASM-safe pointer/scalar accessors; NULL results report out-of-memory. */
int sh_result_status(const sh_result *result);
const char *sh_result_error(const sh_result *result);
const char *sh_result_base62(const sh_result *result);
const char *sh_result_metadata_json(const sh_result *result);
const uint8_t *sh_result_hash(const sh_result *result);
const uint8_t *sh_result_rgba(const sh_result *result);
uint32_t sh_result_hash_length(const sh_result *result);
uint32_t sh_result_base62_length(const sh_result *result);
uint32_t sh_result_rgba_length(const sh_result *result);
uint32_t sh_result_width(const sh_result *result);
uint32_t sh_result_height(const sh_result *result);
#ifdef __cplusplus
}
#endif
#endif
