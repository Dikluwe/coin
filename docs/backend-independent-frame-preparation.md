# External-renderer frame lifecycle

SoRenderManager now exposes additive, non-virtual methods:

```cpp
SbBool prepareFrame(SbBool processDelaySensors = TRUE);
void finishFrame(SbBool rendered = TRUE);
```

Call prepareFrame on the application's rendering/sensor thread, before copying
camera state into an external renderer. Pair each successful preparation with
finishFrame, including unsuccessful backend submissions (pass FALSE). Reentrant
preparation returns FALSE; unmatched completion is a no-op.

prepareFrame processes pending non-idle delay sensors, invokes manager callbacks,
retains the normal audio traversal and computes autoclip through bounding-box,
search and matrix actions. It consumes source redraw requests satisfied by this
frame. finishFrame updates realTime only after success and invokes post callbacks;
notifications produced during traversal or completion remain scheduled.

The existing render()/actuallyRender() GL lifecycle is unchanged. The retained
SoGLRenderAction stores legacy configuration, including viewport size, but these
new entry points do not apply it or issue any GL commands. Host callbacks must
also be backend-independent. They are not an emulation of GL state initialization,
stereo passes or GL superimposition rendering; the external renderer owns those.

CoinRenderManagerAdapter::render wraps its submission with these methods and
uses scoped completion for early backend failure returns. Rebuild the experimental
adapter against this Coin revision: an older core library lacks the new symbols.
Private lifecycle state is kept in SoRenderManager's pimpl, without changing its
public layout or vtable.

Validation targets:

- RenderManagerFramePreparationTest: run without DISPLAY. Sensors, pre/post
  pairing, reentry, camera/autoclip and realTime notifications, zero GL traversals.
- WgpuBgfxRenderManagerAdapterTest and its OpenGL variant: real Xlib window
  presentation/resize with a GL action spy; no auxiliary GL traversal permitted.
- CoinTests, CoinConfigCompatibility and ProfilerInit_*: traditional Coin regressions.

GL inside BGFX's OpenGL renderer is expected; the prohibited dependency is a
separate Coin/Qt GL rendering pass used to prepare an external-renderer frame.
