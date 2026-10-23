# Android GLES: MSAA and lifecycle controls

`validate-coinrender.py` runs the current CoinRender APK with an explicit GLES
renderer and sample count. It checks the cube or city content, native adapter,
HOME/pause, resume in the same process, portrait/landscape/portrait, screenshots,
offscreen checksums and clean BACK termination. It restores the original rotation
setting and saves commands, application logs and the result even after a failure.
Screenshots qualify presentation separately from the 64×64 offscreen capture.
The million scene waits for ten normal frames before HOME, so lifecycle checks
start after queued startup/layout work. This does not qualify responsiveness
during initial loading or establish a performance target.

Build the APK using `.github/scripts/build-coin-render-android-apk.py`, adding
`--include-million-city` for the million scene. Start the emulator with sufficient
RAM (the historical million campaign used 8 GiB), install that APK, then run:

```sh
python3 testsuite/reproducers/android-msaa-read-framebuffer/validate-coinrender.py \
  1 cube --sdk "$HOME/Android/Sdk" --artifacts /absolute/campaign
# Repeat with 4 cube, 1 city, or 1 million; each uses a fresh process.
```

`probe.cpp` is a separate NativeActivity using only EGL/GLES, without Coin or
wgpu. It clears and resolves 1×/4× color/depth attachments, then reads from a
complete READ FBO while DRAW is incomplete. It repeats the read with DRAW bound
to the complete FBO and restores the old binding. On a conforming driver, an
incomplete DRAW FBO must not invalidate a complete READ FBO's `glReadPixels`.
This isolates one translator defect; it does not by itself establish the cause
of every MSAA failure in CoinRender.
The control logs both linked texture-storage entry points with `GL_FRAMEBUFFER`
attachments and `eglGetProcAddress` storage with `GL_DRAW_FRAMEBUFFER` attachments.

```sh
python3 testsuite/reproducers/android-msaa-read-framebuffer/build-probe.py \
  --sdk "$HOME/Android/Sdk" --ndk "$HOME/Android/Sdk/ndk/30.0.16248370" \
  --java-home "$HOME/android-studio/jbr" --output /absolute/probe
adb -s emulator-5554 install --no-incremental -r /absolute/probe/probe.apk
adb -s emulator-5554 shell am start -S -W \
  -n org.coin3d.coinrender.msaaprobe/android.app.NativeActivity
adb -s emulator-5554 logcat -d -s CoinMsaaProbe
```

The probe APK uses a separate package and local development key. No device data
reset or production package removal is required.
