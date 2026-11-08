#!/bin/bash
# Capture all phase results, preserve evidence, then return failure if any phase
# failed. Run a frozen copy of this script; never edit a script while it runs.
set -euo pipefail
main() {
  if [ "$#" -ne 3 ]; then echo 'usage: build-run.sh recording|bgfx|wgpu REPO EVIDENCE_ROOT' >&2; return 2; fi
  local kind="$1" repo="$2" evidence_root="$3"
  local backend
  local -a extra=()
  case "$kind" in
    bgfx) backend=BGFX; extra+=(-DCMAKE_PREFIX_PATH="${COIN_BGFX_PREFIX:-/mnt/Laranja/Git/externos/coin/build-bgfx-recovery/bgfx-prefix}");;
    wgpu) backend=RUST_BRIDGE;;
    recording) backend=RECORDING;;
    *) echo "unknown backend: $kind" >&2; return 2;;
  esac
  local script_dir
  script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
  local out="$evidence_root/$kind"
  local ram="${COIN_RETAINED_BUILD_ROOT:-/dev/shm/coin-retained-presentation-20261108-$kind}"
  local seed="${COIN_RETAINED_SEED_ROOT:-/tmp/coin-retained-presentation-20261108}/$kind/build-state"
  mkdir -p "$out" "$ram"
  # Remove only generated audit data, not build caches or other packages. A
  # failed/new run cannot inherit missing outputs from a successful old run.
  find "$out" -maxdepth 1 -type f \( -name '*.rgba' -o -name '*.depth' -o -name '*.semantic' -o -name '*.exit' -o -name 'controls.junit.xml' -o -name 'validation.json' \) -delete
  if [ ! -d "$ram/build" ]; then
    if [ -d "$out/build-state" ]; then cp -a "$out/build-state" "$ram/build";
    elif [ -d "$seed" ]; then cp -a "$seed" "$ram/build"; fi
  fi
  export TMPDIR="$ram" MESA_SHADER_CACHE_DIR="$ram/mesa-cache" XDG_CACHE_HOME="$ram/cache"
  export CARGO_NET_OFFLINE=true CARGO_BUILD_JOBS=4
  # Keep partial objects even on configure/build failure, without masking the
  # command's exit status. These caches are not accepted as audit receipts.
  preserve() {
    local rc=$?
    trap - EXIT
    set +e
    if [ -d "$ram/build" ]; then
      mkdir -p "$out/build-state"
      cp -a "$ram/build/." "$out/build-state/"
      if [ "$?" -ne 0 ]; then rc=1; fi
    fi
    # The exit receipt also includes failures while preserving caches. A later
    # comparison must not mistake successful render phases for a successful run.
    printf '%s\n' "$rc" > "$out/run.exit" || rc=1
    exit "$rc"
  }
  # The trap runs while main's local variables still exist on explicit exit.
  trap preserve EXIT
  if cmake -S "$repo" -B "$ram/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCOIN_BUILD_RENDER=ON -DCOIN_RENDER_BACKEND="$backend" -DCOIN_BUILD_TESTS=ON \
      -DCOIN_BUILD_DOCUMENTATION=OFF -DCOIN_THREADSAFE=ON -DCOIN_BUILD_SHARED_LIBS=ON \
      "${extra[@]}" > "$out/configure.log" 2>&1; then :; else
    local rc=$?; printf '%s\n' "$rc" > "$out/configure.exit"; tail -25 "$out/configure.log"; exit "$rc"
  fi
  printf '0\n' > "$out/configure.exit"
  local -a targets=(CoinRenderRetainedRevisionAudit CoinRenderRetainedIncrementalTest CoinRenderFrameReuseCoreTest CoinRenderPublicationTest CoinRenderMultiTargetTest CoinRenderFreshnessTest)
  if [ "$kind" = wgpu ]; then targets+=(CoinRenderAsyncActionTest); fi
  if cmake --build "$ram/build" --parallel 5 --target "${targets[@]}" > "$out/build.log" 2>&1; then :; else
    local rc=$?; printf '%s\n' "$rc" > "$out/build.exit"; tail -35 "$out/build.log"; exit "$rc"
  fi
  printf '0\n' > "$out/build.exit"
  export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json LIBGL_ALWAYS_SOFTWARE=1
  if [ "$kind" = recording ]; then unset COIN_RETAINED_TEST_RENDERER; else export COIN_RETAINED_TEST_RENDERER=vulkan; fi
  local exe="$ram/build/bin/CoinRenderRetainedRevisionAudit"
  if [ ! -x "$exe" ]; then echo 'missing audit executable' >&2; exit 1; fi
  local failed=0
  phase() {
    local directory="$1" name="$2"; shift 2
    local code=0
    if "$@" > "$directory/$name.log" 2>&1; then :; else code=$?; failed=1; fi
    printf '%s\n' "$code" > "$directory/$name.exit"
    echo "$kind $name status=$code"
  }
  phase "$out" audit flock /tmp/coin-retained-audit-gpu.lock "$exe" "$out"
  phase "$out" controls flock /tmp/coin-retained-audit-gpu.lock ctest --test-dir "$ram/build" \
    -R '^(CoinRenderRetainedIncrementalTest|CoinRenderFrameReuseCoreTest|CoinRenderPublicationTest|CoinRenderMultiTargetTest|CoinRenderFreshnessTest|CoinRenderAsyncActionTest)$' \
    --verbose --output-on-failure --parallel 1 --output-junit "$out/controls.junit.xml"
  if [ "$kind" = bgfx ]; then
    mkdir -p "$out/pipeline3"
    find "$out/pipeline3" -maxdepth 1 -type f -delete
    phase "$out/pipeline3" audit env COIN_BGFX_READBACK_PIPELINE_DEPTH=3 flock /tmp/coin-retained-audit-gpu.lock "$exe" "$out/pipeline3"
  fi
  cp "$ram/build/CMakeCache.txt" "$out/CMakeCache.txt"
  mkdir -p "$out/bin" "$out/lib"
  find "$ram/build" -name 'libCoin*.so*' -type f -exec cp -L '{}' "$out/lib/" \;
  for program in "${targets[@]}"; do cp "$ram/build/bin/$program" "$out/bin/"; done
  ln -sf libCoin.so.80.0.10 "$out/lib/libCoin.so.80"
  printf '%s\n' "$failed" > "$out/run.exit"
  if ! python3 "$script_dir/validate-run.py" "$out" --backend "$kind" > "$out/validation.log" 2>&1; then failed=1; fi
  if [ "$kind" = bgfx ]; then
    if ! python3 "$script_dir/validate-run.py" "$out/pipeline3" --backend bgfx --audit-only > "$out/pipeline3/validation.log" 2>&1; then failed=1; fi
  fi
  printf '%s\n' "$failed" > "$out/run.exit"
  tail -3 "$out/audit.log"
  # Explicit exit preserves local scope for the cache trap and propagates every
  # failed phase/validator, even when the final copy/ln commands succeeded.
  exit "$failed"
}
main "$@"
