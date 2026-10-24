# WebGPU city on the TCL browser — 2026-10-23

The ordinary TCL BrowseHere browser rendered the explicit 40,000-building control
plus its ground cube: 40,001 submitted instances and 480,012 logical triangles.
Coin/CoinRender captures geometry, transforms and materials in WebAssembly; Rust
wgpu uses BrowserWebGpu only, with the explicit compatibility profile. The actual
adapter reported vendor `arm` and `isFallbackAdapter=false`. The canvas was
1322 × 857. A screenshot confirmed visible buildings and ground; queue completion
alone was not used as evidence of visible rendering.

The device is the Android 11 TCL Smart TV Pro (mt5889, Mali-G52, 32-bit ARM), with
WebView 155.0.8059.43. This is a static demonstration shader and captured instancing
profile, not full native CoinRender shader or scene-feature qualification.

## Observations and comparison

| Device/path | Buildings | Preparation | Measured result |
| --- | ---: | ---: | --- |
| TCL BrowseHere / WebGPU compatibility | 40,000 | 17.02 s, assets cached | 8.50 FPS median; 8.43–9.80 FPS across the first 12 groups |
| TCL BrowseHere / WebGPU compatibility | 1,000,000 | 365.68 s | Not qualified: completed submissions but uniform clear image |
| Android x86_64 emulator / native wgpu GLES, host AMD Renoir | 1,000,000 | Separate native measurement | Previously 45.80–48.73 FPS, 1080 × 2400 |
| LG G7 / native backend | 1,000,000 | Initialization/memory failures | No valid FPS sample |

The final TCL 40k observation discards 10 GPU-completed warmup frames and measures
120 further frames in 12 groups of 10, in one browser navigation. Median group
wall interval is 117.65 ms/frame. Earlier 40k navigation prepared in 19.73 s;
its first three groups were about 10 FPS before later groups fell toward 8.5 FPS.
These short observations do not establish thermal behavior or long-term stability.
The final preparation includes 14.20 s Coin capture, 0.64 s upload/validation and
0.16 s asset load; total also includes initialization and scheduling.

The million attempt took 346.38 s in Coin capture, 10.02 s in upload/validation
and 3.04 s loading assets. It submitted all 1,000,001 instances and completed
40 frames, but the screenshot contained only the clear color. Its approximately
0.48 completed submissions/s must not be quoted as successful rendering FPS.
The visible 40k control establishes that the failure is associated with the
larger workload; it does not identify the cause as Coin, wgpu, browser or driver.
A diagnostic using smaller draw batches was not completed on the TV. The
million-scene path does render visible geometry in the host browser tests, but
remains unqualified on this TCL. It is not the TV demonstration entry point.

The emulator reference is documented in
[the bridge comparison](coin-render-android-bridge-reuse-20261023.md).
The TV and emulator differ in GPU/CPU, runtime, resolution, shader path and timing
method; no hardware or CoinRender speedup ratio follows from these results.
In particular, the TV 40k result cannot substitute for a million-scene comparison,
and G7's missing sample is not zero FPS.

## Private access

The temporary public HTTPS tunnel was stopped. The operational browser path uses
an authorized ADB reverse connection to a server bound only to PC loopback:

```sh
python3 examples/coinrender/webgpu/serve.py --directory /path/to/built/site \
  --loopback-http --bind 127.0.0.1 --port 8089
adb -s 192.168.0.18:5555 reverse tcp:8092 tcp:8089
```

Open `http://127.0.0.1:8092/?profile=compatibility&scene=city-40000` in BrowseHere.
This is the TV's own trustworthy loopback origin, forwarded to the PC. It uses no
helper viewer, certificate bypass, WebGL fallback or public tunnel. It depends on
the ADB connection and server remaining available; it is not standalone LAN HTTPS.
The million selector remains explicit for further diagnostics, never an automatic
replacement for or reduction to the 40k scene.
