# Android TV scoped HTTPS diagnostic viewer

This separate app displays the existing CoinRender HTTPS site in the device's
installed WebView. It embeds only the supplied **public X.509 CA certificate**
and trusts only that CA for the exact configured URL host. The app's default
trust-anchor set is empty; no system/user CA fallback or debug trust override is
added. Navigation, document resources and worker fetches are restricted to the
fixed HTTPS origin (scheme, host and effective port). TLS hostname/chain checks
remain enabled, and SSL errors are cancelled. The app changes no system trust
store, browser profile or GPU settings and has no JavaScript bridge.

The package is `org.coin3d.coinrender.webgpuviewer`. Its only permission is
`INTERNET`. It supports the TV launcher, landscape and hardware acceleration.
The APK declares minSdk 23 and targetSdk 30; Android 6/API 23 refuses to load the
page because it cannot enforce Network Security Config. Android 7/API 24 or
later is required for the scoped trust policy. This is a diagnostic viewer,
not a promise that a particular WebView exposes WebGPU or a usable adapter.

Build using a JDK and an Android SDK with a platform `android.jar` and build-tools
(`aapt2`, `d8`, `zipalign`, `apksigner`). No Gradle or third-party Java library is
needed. The builder validates CA dates/basicConstraints and exports exactly one
public certificate as DER; private keys or extra certificate data are rejected.
All build state and the generated signing key/password remain under the required
RAM build directory. Reusing that directory reuses its signing identity for
replacement diagnostic builds. The final APK is copied to the requested output.

```sh
python3 examples/coinrender/webgpu/android_viewer/build.py \
  --sdk "$HOME/Android/Sdk" \
  --build-dir /dev/shm/coin-tv-viewer/build \
  --output-apk /dev/shm/coin-tv-viewer/viewer.apk \
  --ca /path/to/public/CoinRender-Local-CA.crt \
  --url 'https://192.168.0.16:8443/?profile=compatibility'
adb -s 192.168.0.18:5555 install -r /dev/shm/coin-tv-viewer/viewer.apk
adb -s 192.168.0.18:5555 shell am start \
  -n org.coin3d.coinrender.webgpuviewer/.MainActivity
adb -s 192.168.0.18:5555 logcat -s CoinRenderViewer
```

`PAGE_STATE` is the actual page's evaluated URL, `isSecureContext`, presence of
`navigator.gpu`, and `window.coinRenderWebGPU`, sampled every three seconds.
Loading a document is never reported as GPU success. Read the page's phase/error
and confirmed frame count; missing `navigator.gpu` or a rejected adapter remains
an actual limitation. Native load/TLS errors appear visibly and in logcat.

For remote WebView inspection, build explicitly with `--debug`. Only a debuggable
APK enables WebView debugging; the default build disables it. This does not enable
an experimental GPU API. To verify trust rejection, build using a different valid
public CA, replace this diagnostic package and confirm that TLS fails; rebuild
with the intended public CA using the same RAM build directory for the positive
case. Certificates/keys must never be served with the site or committed to Git.
Remove this diagnostic package when finished:

```sh
adb -s 192.168.0.18:5555 uninstall org.coin3d.coinrender.webgpuviewer
```

## Tested TCL TV

The app is labelled **CoinRender HTTPS viewer** on the TV. On the tested TCL
Android 11 / 32-bit Mali-G52 TV with WebView 155, the supplied CA allowed strict
HTTPS with `isSecureContext=true`; replacing it with a different CA caused TLS
rejection. This trust is confined to this app and does not install the CA in
the system or the TCL browser. The Core adapter was unavailable. The explicit
`profile=compatibility` URL rendered the captured cube through Rust wgpu/WebGPU,
with 24 vertices, 36 indices, advancing confirmed frames, ARM adapter identity
and `isFallbackAdapter=false`. No WebGL or software fallback was used. This
qualifies the demonstration on that tested configuration, not full native
CoinRender support or the browser's underlying Vulkan backend.
