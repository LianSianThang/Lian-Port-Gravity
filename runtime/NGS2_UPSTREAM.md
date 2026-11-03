# NGS2 runtime attribution

`src/runtime_ngs2.c` and the baseline `tests/test_ngs2.c` are adapted from
[gminustwo/minustwo](https://github.com/gminustwo/minustwo), downloaded 2026-11-02.
The upstream repository is licensed under GNU GPL version 2; this repository
also includes that licence in `LICENSE`. Preserve upstream attribution when
redistributing modified source or builds.

Upstream main revision recorded at download:
`6d6ebce1346fa5f2f532a0ff3cd68321c13bbc0c`.

The mixer implements PS4 NGS2 sampler voices (ATRAC9/PCM), streamed waveform
blocks, resampling, envelopes, routing and mastering. Its effects limitations
are documented at the top of the source. Successful upstream tests do not
establish Gravity Rush compatibility.

Local adaptations add Gravity Rush's allocator-based system/rack entry points,
bound option tracing by the supplied structure size, normalize its numeric
ATRAC9 config word, and gate native binding with `LIAN_NGS2_NATIVE=1` while
integration is verified. Explicit-buffer APIs keep caller ownership; allocator
APIs release through the guest callback. Original game archives are untouched.
The adaptation also preserves queued blocks on Gravity's empty-list close and
accepts its default rack-input patch destinations (`destInputId=0`).
Full flag-1 stream additions now append complete descriptors with per-block
sample counts; flag-3 data-only additions retain the first descriptor's total
count. Reused sampler setup resets its old queue index. Bounded decoder-error
metadata and opt-in voice statistics support remaining compatibility work.

`src/runtime_ngs2_codec.c/.h` and `tests/test_ngs2_codec.c` are a separate local
decoder component/test. The adapted mixer currently retains its own upstream
decoder and streaming scheduler; the standalone codec is not wired into it.
