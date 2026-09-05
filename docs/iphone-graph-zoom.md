# iPhone Fit graph / zoom investigation

## What was confirmed

A user reported a page crash while combining **Fit graph** and zoom on an
iPhone. During investigation, the ESP32 had been up for about 12 hours and was
still serving current, non-degraded topology data. This points toward browser
rendering, but does not by itself prove the cause of the phone's crash.

Two defects were reproduced in the released v0.8.0 renderer:

1. Every graph render scheduled an untracked Canvas-start animation callback.
   Two renders in one frame started two drawing loops. The shared state held
   only the last loop's ID, so teardown cancelled one and left the other alive.
   Subsequent redraws could retain detached canvases through those callbacks.
2. `getContext("2d")` was used without a null check. Simulating a failed Canvas
   allocation produced `TypeError: Cannot read properties of null (reading
   'scale')` rather than a controlled rendering fallback.

Fit only changed the CSS transform: it did not shrink the underlying bitmap.
For example, a synthetic 1850 × 1800 graph with a backing ratio of 2 allocated
53,280,000 bytes (about 50.8 MiB) for each RGBA Canvas, before compositor or
other browser costs. This is an example, not a measured iPhone memory reading.

WebKit has documented historical cases where Canvas memory pressure caused
context allocation to return null. Those reports support treating this failure
as possible; they do not establish a universal current iOS limit or prove this
particular user's crash. [WebKit issue 195325](https://bugs.webkit.org/show_bug.cgi?id=195325).

Run the deterministic, router-free old-code reproducer from this repository:

```sh
node tests/reproduce_v080_canvas.js
```

It requires a Git checkout containing the `v0.8.0` tag and loads
`v0.8.0:mesh_web/app.js` into a small test fixture. Expected
output includes `animationLoopsStarted: 2`, `loopsLeftAfterTeardown: 1`, and
the allocation-failure TypeError. It does not launch or crash a phone browser.

## Fix in v0.8.1

- Draw connections as SVG paths and endpoint circles, without a Canvas,
  `getContext`, filters, or a perpetual JavaScript animation loop.
- Keep the original curve geometry, band colors, link labels, current/desired
  parent previews, and the complete node/client/debug data.
- Remove the decorative moving packets. They were not measurements of real
  traffic; topology polling and recovery countdowns are unchanged.
- Coalesce the remaining one-shot layout resize callback. Browser pinch zoom
  is left to the browser; MeshScope does not listen to visual-viewport changes
  to resize bitmaps or multiply the graph scale.
- Fit the complete graph even when it needs less than 10% scale. Restore sane
  map dimensions when the last online gateway disappears.

SVG still requires browser rendering resources. The fix removes the specific
large Canvas buffers and orphan animation-loop mechanism; it is not a claim
that arbitrary diagrams have zero memory cost.

## Validation and limits

- JavaScript and Python regression tests; deterministic embedded gzip assets.
- An isolated 23-node topology (22 online, one offline), including a four-hop
  subtree, recovery information, wired links, and clients, under the ESP32 CSP.
- Twenty Fit/100% cycles, interleaved refreshes, and 375/390/430px portrait and
  844px landscape viewports. Each check retains 22 graph nodes and 22 links
  including WAN, exactly one SVG, zero canvases, and no body-width overflow.
- macOS Safari page zoom with Fit active, in addition to the in-app browser.

There was no physical iPhone crash log or remote Web Inspector session during
this investigation. Desktop Safari zoom and simulated phone widths are not
equivalent to real iPhone pinch gestures or memory pressure. If the problem
persists, include the iPhone/iOS version, browser, exact gesture, whether Safari
reloads the page, and whether `/api/status` remains reachable. Do not post router
passwords or Client/STA identities with the report.

To run the larger offline preview yourself:

```sh
python3 tests/serve_workspace_preview.py --port 8781 --scenario recovery --extra-nodes 16
```

Open `http://127.0.0.1:8781/`. All writes target synthetic in-memory data; no
router restart, steering, or throughput test is performed.
