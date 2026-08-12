# Prefetch engine status

The prefetch engine now learns observed tensor transitions and returns
deterministically ranked predictions. It also reports cache residency hits
and misses without inventing successful loads.

Actual loading of an uncached tensor is intentionally not performed by
`PrefetchEngine::prefetch()`: the current interface supplies only tensor names,
while `VPIDWorkspace` requires an offset and byte size. Callers with tensor
metadata should use `VPIDWorkspace::read_direct()` (or
`VPIDWorkspace::prefetch(offset, size)`) to load data. This limitation is
covered by `tests/prefetch_engine_test.cpp` and must remain explicit until a
tensor metadata registry is added.
