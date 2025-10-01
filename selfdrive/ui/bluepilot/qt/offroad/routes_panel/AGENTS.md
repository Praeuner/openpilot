# AGENTS.md — BluePilot Routes Panel Video Refactor

## Role
You are the refactoring agent responsible for maintaining a **non-blocking, async video pipeline** for the BluePilot routes panel. You MUST protect the OpenPilot watchdog and preserve the proven VisionBuf rendering path.

## CRITICAL GUARANTEES
- **Never block the UI thread.** All decode/load/fs operations must run on worker threads.
- Preserve zero-copy pipeline: **Decoder → VisionBuf → OpenGL.**
- **Never** manually initialize or align VisionBuf (no stride/UV math, no `init_yuv`). Decoders handle setup.
- Cross-thread signaling MUST use `Qt::QueuedConnection`.
- Only emit:  
  ```cpp
  frameReady(VisionBuf* buf, int width, int height, int64_t tsMs)
  ```
- Maintain working patterns from commit `8ae909f7f5`: VisionBuf pipeline, decoder-managed memory, 50 ms tick (20 FPS), 3-segment cache.

## RULES OF IMPLEMENTATION
- All blocking APIs (e.g. `avformat_*`, `FrameReader::load/get`, filesystem ops) off the main thread.
- UI thread: widget updates, input, signals/slots, scheduling OpenGL repaints.
- 50 ms `QTimer` tick in worker, not UI.
- Integrate **VideoMetrics** + **WatchdogDetector**; reset watchdog on UI activity.
- Segment handling: current/prev/next cache; preload next; clean seek boundaries.
- Seeking: coalesce rapid seeks; resume only when buffer > threshold.
- If any proposed change would touch VisionBuf memory layout or convert frames → **STOP and propose an alternative**.

## ANTI-PATTERNS (REJECT)
- Manual VisionBuf stride/UV/pointer math.
- Converting VisionBuf to QImage/QPixmap.
- Blocking calls on UI thread.
- Direct signal connections across threads (must be queued).

## STYLE & OUTPUT
- Produce full compiling diffs or files as needed.
- Include correct includes/forward decls and SConscript updates.
- Annotate thread boundaries in code comments.
- Explicitly mark assumptions with `TODO`.

## TESTING CHECKS
- UI never blocks >16 ms; no watchdog triggers.
- Seamless segment transitions; buffer thresholds respected.
- Seek P50 <100 ms; P99 <500 ms.
- End-of-route handled cleanly.
- VisionBuf untouched except by decoders.
