/** Adapter for the Emscripten module built via `npm run build` (see Dockerfile).
 * All returned typed arrays are copies, independent of WASM memory growth/free.
 * No global module, no struct offsets, no retained input pointers.
 */
export function createSkeleHashAPI(Module) {
  function collect(result) {
    if (!result) throw new Error('SkeleHash: out of memory');
    try {
      const status = Module._sh_result_status(result);
      if (status !== 0) {
        const error = new Error(Module.UTF8ToString(Module._sh_result_error(result)));
        error.status = status;
        throw error;
      }
      const readBytes = (pointer, length) => pointer
        ? Module.HEAPU8.slice(pointer, pointer + length) : null;
      return {
        status,
        base62: Module.UTF8ToString(Module._sh_result_base62(result)),
        metadata: JSON.parse(Module.UTF8ToString(Module._sh_result_metadata_json(result))),
        hash: readBytes(Module._sh_result_hash(result), Module._sh_result_hash_length(result)),
        rgba: readBytes(Module._sh_result_rgba(result), Module._sh_result_rgba_length(result)),
      };
    } finally {
      Module._sh_result_free(result);
    }
  }
  return {
    encodeRGBA(width, height, rgba) {
      if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 || width > 200 || height > 200)
        throw new RangeError('Dimensions must be integers 1..200');
      if (!(rgba instanceof Uint8Array || rgba instanceof Uint8ClampedArray) || rgba.length !== width * height * 4)
        throw new TypeError('Expected exactly width*height*4 RGBA bytes');
      // Snapshot first: malloc may grow WASM memory and detach a supplied heap view.
      const input = Uint8Array.from(rgba);
      const pointer = Module._malloc(input.length);
      if (!pointer) throw new Error('SkeleHash: out of memory');
      try {
        Module.HEAPU8.set(input, pointer);
        return collect(Module._sh_encode_rgba(width, height, pointer, input.length));
      } finally { Module._free(pointer); }
    },
    decodeBase62(text) {
      if (typeof text !== 'string' || text.length < 1 || text.length > 256 || !/^[1-9A-Za-z][0-9A-Za-z]*$/.test(text))
        throw new TypeError('Expected canonical Base62 text (1..256 ASCII characters)');
      const pointer = Module._malloc(text.length + 1);
      if (!pointer) throw new Error('SkeleHash: out of memory');
      try {
        for (let i = 0; i < text.length; i++) Module.HEAPU8[pointer + i] = text.charCodeAt(i);
        Module.HEAPU8[pointer + text.length] = 0;
        return collect(Module._sh_decode_base62(pointer, text.length));
      } finally { Module._free(pointer); }
    },
  };
}
