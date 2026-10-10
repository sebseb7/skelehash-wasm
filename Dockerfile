FROM emscripten/emsdk AS build-stage

COPY src /src

RUN mkdir -p /src/build && emcc /src/skelehash.c -std=c11 -O2 -lm --no-entry \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_FUNCTIONS='["_malloc","_free","_sh_encode_rgba","_sh_decode_base62","_sh_result_free","_sh_result_status","_sh_result_error","_sh_result_base62","_sh_result_metadata_json","_sh_result_hash","_sh_result_rgba","_sh_result_hash_length","_sh_result_base62_length","_sh_result_rgba_length","_sh_result_width","_sh_result_height"]' \
  -sEXPORTED_RUNTIME_METHODS='["UTF8ToString","HEAPU8"]' \
  -sEXPORT_NAME=createSkeleHashModule \
  -o /src/build/skelehash.js

# Output stage
FROM scratch
COPY --from=build-stage /src/build/skelehash.js /
COPY --from=build-stage /src/build/skelehash.wasm /
