import { parentPort } from 'node:worker_threads';
import { decodeBase62, encodeDecode, encodeRGBA } from './index.js';

function serializeError(error) {
  return {
    name: error?.name || 'Error',
    message: error?.message || String(error),
    stack: error?.stack,
    status: error?.status,
  };
}

parentPort.on('message', async ({ id, operation, payload }) => {
  try {
    let result;
    if (operation === 'encode') result = await encodeRGBA(payload.width, payload.height, payload.rgba);
    else if (operation === 'decode') result = await decodeBase62(payload.base62);
    else if (operation === 'roundtrip') result = await encodeDecode(payload.width, payload.height, payload.rgba);
    else throw new TypeError(`Unknown SkeleHash operation: ${operation}`);

    parentPort.postMessage({ id, result });
  } catch (error) {
    parentPort.postMessage({ id, error: serializeError(error) });
  }
});
