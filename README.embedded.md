# Embedded CLI wrapper

This fork keeps the upstream `llama.cpp` layout intact and adds a tiny in-process CLI so you can use the server features (chat, completions, embeddings, rerank, tokenize, etc.) without starting an HTTP listener. The goal is to make it easy to embed llama.cpp in local apps or Python subprocesses while staying close to upstream for painless updates.

## What is included
- New binary `llama-embedded-cli` built from the existing server stack (no HTTP server started)
- Supports the same JSON payloads as `llama-server` routes: chat/completions, embeddings, rerank, tokenize/detokenize, apply-template, props/health
- Works with stdin or files for raw JSON bodies, or simple `--text` / `--query` / `--document` helpers for common tasks
- Default keeps upstream `README.md`; this file documents the embedded CLI additions only

## Building
Server tooling must be enabled (default in standalone builds):

```sh
cmake -B build -S . -DLLAMA_BUILD_SERVER=ON -DLLAMA_BUILD_TOOLS=ON
cmake --build build --target llama-embedded-cli
```

The target lives in `tools/embedded-cli` and will be included automatically when `LLAMA_BUILD_SERVER` is on.

## Usage (read this first)
`llama-embedded-cli` has **two layers of flags**:

- Embedded CLI layer (how you tell it *what to do* and *what text/JSON to use*)
- Normal llama.cpp layer (all the usual model / GPU / context options)

Anything it does **not** recognize as an embedded-CLI flag is passed straight through to the upstream llama.cpp argument parser. That means all of this still works:

```sh
# Bigger context + partial GPU offload
./llama-embedded-cli chat --text "hi" \
  -m models/model.gguf \
  --ctx-size 16384 \
  --gpu-layers 40
```

On macOS, if you unpack the zip into `build/bin`, run it from there so the Metal/Vulkan DLLs are on the loader path:

```sh
cd ~/Downloads/embedded-llama-bin/build/bin
DYLD_LIBRARY_PATH=$(pwd) ./llama-embedded-cli --help-cli
```

### Help flags

- `./llama-embedded-cli --help-cli` → **embedded CLI usage + examples** (chat/embeddings/rerank/tokenize).
- `./llama-embedded-cli --help` or `-h` → full upstream llama.cpp/server flags (ctx-size, gpu-layers, hf-repo, etc.).

If you’re embedding this binary into an app, treat `--help-cli` as the canonical contract for how to call it.

### Core operations

All routes are selected via `--op` / `--mode` / `--route` **or** by using the first positional argument (`chat`, `completion`, `embeddings`, `rerank`, `tokenize`, etc.).

#### Chat

One-shot chat using plain text:

```sh
./llama-embedded-cli chat \
  --text "hello from embedded-cli" \
  -m models/chat-model.gguf \
  --no-stream           # or --stream for incremental tokens
```

Equivalent JSON body (same as `/v1/chat/completions`):

```sh
echo '{"messages":[{"role":"user","content":"hello"}],"stream":false}' | \
  ./llama-embedded-cli chat --stdin -m models/chat-model.gguf
```

#### Completion

```sh
./llama-embedded-cli completion \
  --text "Once upon a time" \
  -m models/model.gguf \
  --no-stream
```

This builds a JSON body like:

```jsonc
{ "prompt": "Once upon a time", "stream": false }
```

#### Embeddings

```sh
./llama-embedded-cli embeddings \
  --text "embed this" \
  -m models/embedding-model.gguf
```

This is equivalent to the embeddings HTTP route and builds:

```jsonc
{ "input": "embed this" }
```

You can also send the full JSON yourself:

```sh
echo '{"input":["foo","bar"]}' | \
  ./llama-embedded-cli embeddings --stdin -m models/embedding-model.gguf
```

#### Rerank

```sh
./llama-embedded-cli rerank \
  --query "python" \
  --document "Python is great." \
  --document "I like Rust." \
  --top-n 1 \
  -m models/rerank-model.gguf
```

Or with a documents file:

```sh
./llama-embedded-cli rerank \
  --query "python" \
  --documents-file docs.txt \
  --top-n 3 \
  -m models/rerank-model.gguf
```

The rerank payload looks like:

```jsonc
{
  "query": "python",
  "documents": ["Python is great.", "I like Rust."],
  "top_n": 1
}
```

#### Tokenize / detokenize

```sh
./llama-embedded-cli tokenize \
  --text "tokenize this" \
  -m models/model.gguf

./llama-embedded-cli detokenize \
  --body '{"tokens":[1, 2, 3]}' \
  -m models/model.gguf
```

You can always fall back to sending the exact JSON body the HTTP API expects and using `--body`, `--body-file`, or `--stdin`.

### Embedded-CLI-specific flags

These are parsed first; everything else is forwarded unchanged to llama.cpp:

- `--op` / `--mode` / `--route` / first positional: route name (`chat`, `completion`, `embeddings`, `rerank`, `tokenize`, `detokenize`, `apply-template`, `props`).
- `--text`, `-t`: plain text for chat/completion/embeddings/tokenize when not providing JSON.
- `--body`, `--json`, `--input-json`: inline JSON string.
- `--body-file`, `--json-file`: file with JSON request body.
- `--stdin`: read JSON request body from stdin.
- `--query`: rerank query text (falls back to `--text` / `-p`).
- `--document`, `--doc`: add a rerank document (repeatable).
- `--documents-file`: newline-delimited rerank documents.
- `--top-n`: rerank cutoff (optional).
- `--stream` / `--no-stream`: override the `stream` flag for chat/completion when the body is auto-built.
- `--help-cli`: print this embedded-CLI usage and exit.

### Normal llama.cpp flags (ctx, GPU, etc.)

Everything not listed above is treated as a normal llama.cpp/server flag and forwarded untouched. Common ones:

- `-m`, `--model`: model path (`.gguf`).
- `--ctx-size`, `--n_ctx`: context length.
- `--gpu-layers`, `-ngl`, `--n-gpu-layers`: number of layers to offload to GPU.
- `--threads`, `--n-threads`, `--n-parallel`, etc.
- Any other upstream server/CLI flag you see in `./llama-embedded-cli --help`.

That means you can safely embed this binary in your app and still control context length and GPU offload exactly like you would with `llama-server` or `llama-cli`—the embedded CLI just adds a thin “routing + input” layer on top.

## Notes
- The upstream README remains untouched (`README.md`) for easier rebases; this file documents the embedded CLI surface.
- Rerank auto-sets `--embedding` and `pooling_type=rank` for you. Embeddings auto-enable `--embedding`.
- No ports or sockets are opened: everything runs in-process against the llama.cpp server pipeline.

## Upstream updates
Run `python scripts/embedded-cli/reapply_overlays.py` after pulling upstream. It re-adds the README banner and the `embedded-cli` CMake hook if a merge overwrote them.
