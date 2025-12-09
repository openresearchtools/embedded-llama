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

## Usage
`llama-embedded-cli` accepts the same model flags as `llama-server`/`llama-cli`, plus a few helpers:

```sh
# Chat/completions in one shot (no streaming by default unless you add --stream)
./llama-embedded-cli chat --text "hello" -m models/foo.gguf --no-stream

# Embeddings
./llama-embedded-cli embeddings --text "embed this" -m models/foo.gguf

# Rerank with two documents
./llama-embedded-cli rerank --query "python"   --document "Python is great."   --document "I like rust."   --top-n 1 -m models/foo.gguf

# Feed a full JSON request (same shape as the HTTP API)
./llama-embedded-cli chat --body-file request.json -m models/foo.gguf
```

Key options (in addition to the normal llama flags such as `-m`, `--n-gpu-layers`, etc.):
- `--op <chat|completion|embeddings|rerank|tokenize|detokenize|apply-template|props>` pick a route (positional first arg also works)
- `--text` plain text for chat/completions/embeddings/tokenize
- `--query`, `--document`/`--documents-file`, `--top-n` helpers for rerank
- `--body`, `--body-file`, `--stdin` send raw JSON matching the REST payloads
- `--stream` / `--no-stream` override the `stream` flag when auto-building chat/completion bodies

If you need a route not covered by the helpers, pass the exact JSON body you would send to `llama-server` via `--body`, `--body-file`, or `--stdin`.

## Notes
- The upstream README remains untouched (`README.md`) for easier rebases; this file documents the embedded CLI surface.
- Rerank auto-sets `--embedding` and `pooling_type=rank` for you. Embeddings auto-enable `--embedding`.
- No ports or sockets are opened: everything runs in-process against the llama.cpp server pipeline.

## Upstream updates
Run `python scripts/embedded-cli/reapply_overlays.py` after pulling upstream. It re-adds the README banner and the `embedded-cli` CMake hook if a merge overwrote them.
