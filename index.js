import createSkeleHashModule from './skelehash.js';
import { createSkeleHashAPI } from './adapter.js';

let codecPromise;

function getCodec() {
  if (!codecPromise) {
    codecPromise = createSkeleHashModule().then((Module) => createSkeleHashAPI(Module)).catch((error) => {
      codecPromise = undefined;
      throw error;
    });
  }
  return codecPromise;
}

function toRGBABytes(source) {
  if (source instanceof Uint8Array) return source;
  if (source instanceof Uint8ClampedArray) return new Uint8Array(source.buffer, source.byteOffset, source.byteLength);
  if (source instanceof ArrayBuffer) return new Uint8Array(source);
  if (typeof Buffer !== 'undefined' && Buffer.isBuffer(source)) return new Uint8Array(source);
  throw new TypeError('Expected Uint8Array, Uint8ClampedArray, ArrayBuffer, or Buffer with width*height*4 RGBA bytes');
}

/**
 * Encodes unpremultiplied RGBA bytes into a SkeleHash (custom TH03 format).
 *
 * @param {number} width - Image width, integer 1..200
 * @param {number} height - Image height, integer 1..200
 * @param {Uint8Array|Uint8ClampedArray|ArrayBuffer|Buffer} rgba - Exactly width*height*4 bytes
 * @returns {Promise<{ status: 0, base62: string, hash: Uint8Array, rgba: null, metadata: object }>}
 */
export async function encodeRGBA(width, height, rgba) {
  const codec = await getCodec();
  return codec.encodeRGBA(width, height, toRGBABytes(rgba));
}

/**
 * Decodes a SkeleHash Base62 string into unpremultiplied RGBA bytes.
 * Output has a 64px maximum edge. Lossy placeholder, not the original image.
 *
 * @param {string} base62 - Canonical Base62 text (1..256 ASCII characters)
 * @returns {Promise<{ status: 0, base62: string, hash: Uint8Array, rgba: Uint8Array, metadata: object }>}
 */
export async function decodeBase62(base62) {
  const codec = await getCodec();
  return codec.decodeBase62(base62);
}

/**
 * Round-trip helper: encode RGBA, then decode the resulting hash.
 *
 * @returns {Promise<{ encoded, decoded }>}
 */
export async function encodeDecode(width, height, rgba) {
  const encoded = await encodeRGBA(width, height, rgba);
  const decoded = await decodeBase62(encoded.base62);
  return { encoded, decoded };
}

export { createSkeleHashAPI };
export default encodeRGBA;
