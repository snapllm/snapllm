# Streaming and admission loop — 2026-08-11

## Behavior change

`POST /v1/chat/completions` now defaults to SSE streaming when `stream` is
omitted. Clients that need the previous buffered JSON response can send
`"stream": false`.

## Live evidence

- Default request without `stream` returned `Content-Type: text/event-stream`.
- Explicit `"stream": false` returned `application/json`.
- Sixteen concurrent chat requests completed with `16/16` HTTP 200 responses.
- Maximum observed latency for the 16-request CPU test was `13.79s`.
- The local daemon was stopped after the test.

The server remains bounded: HTTP worker admission is finite, while inference
is serialized by default for GPU safety. Requests beyond the bounded queue are
rejected rather than buffered without limit.
