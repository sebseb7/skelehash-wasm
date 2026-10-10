import {
  createSkeleHashAPI,
  decodeBase62 as decodeBase62Sync,
  encodeDecode as encodeDecodeSync,
  encodeRGBA as encodeRGBASync,
} from './index.js';
import { runInWorker } from './node-worker-client.js';

/**
 * Encodes RGBA in a worker thread so WASM does not block Node.js's
 * main event loop.
 */
export function encodeRGBA(width, height, rgba) {
  return runInWorker('encode', { width, height, rgba }, {});
}

/**
 * Decodes a SkeleHash Base62 string in a worker thread.
 */
export function decodeBase62(base62) {
  return runInWorker('decode', { base62 }, {});
}

/**
 * Round-trip helper in a worker thread.
 */
export function encodeDecode(width, height, rgba) {
  return runInWorker('roundtrip', { width, height, rgba }, {});
}

export { createSkeleHashAPI };

// Explicit escape hatches for callers already running inside a worker.
export { decodeBase62Sync, encodeDecodeSync, encodeRGBASync };

export default encodeRGBA;
