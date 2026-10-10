import { Worker } from 'node:worker_threads';

let worker;
let nextRequestId = 1;
const pending = new Map();

const ERROR_TYPES = { Error, RangeError, TypeError, SyntaxError, URIError, ReferenceError, EvalError };

function deserializeError(serialized) {
  const Constructor = ERROR_TYPES[serialized.name] || Error;
  const error = new Constructor(serialized.message);
  if (serialized.stack) error.stack = serialized.stack;
  if (serialized.status !== undefined) error.status = serialized.status;
  return error;
}

function rejectAll(error) {
  for (const { reject } of pending.values()) reject(error);
  pending.clear();
}

function getWorker() {
  if (worker) return worker;

  worker = new Worker(new URL('./node-worker.js', import.meta.url));
  worker.unref();

  worker.on('message', ({ id, result, error }) => {
    const request = pending.get(id);
    if (!request) return;

    pending.delete(id);
    if (pending.size === 0) worker?.unref();

    if (error) request.reject(deserializeError(error));
    else request.resolve(result);
  });

  worker.on('error', (error) => {
    rejectAll(error);
  });

  worker.on('exit', (code) => {
    const exitedWorker = worker;
    worker = undefined;
    if (pending.size > 0) {
      rejectAll(new Error(`SkeleHash worker stopped unexpectedly with exit code ${code}`));
    }
    exitedWorker?.removeAllListeners();
  });

  return worker;
}

function copyPayload(payload) {
  const out = { ...payload };
  if (payload.rgba instanceof Uint8Array) out.rgba = payload.rgba.slice();
  else if (payload.rgba instanceof Uint8ClampedArray) out.rgba = new Uint8Array(payload.rgba);
  else if (payload.rgba instanceof ArrayBuffer) out.rgba = new Uint8Array(payload.rgba.slice(0));
  return out;
}

export function runInWorker(operation, payload) {
  const worker = getWorker();
  worker.ref();
  const id = nextRequestId++;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    worker.postMessage({ id, operation, payload: copyPayload(payload) });
  });
}
