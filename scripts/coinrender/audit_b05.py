#!/usr/bin/env python3
"""Audit the B05 Xwayland EGL lifecycle fix and its preserved negative controls."""
import csv,collections,hashlib,json,re,sys
from pathlib import Path

def require(condition,message):
 if not condition:raise ValueError(message)

def main():
 root=Path(sys.argv[1]);manifest=json.loads((root/'manifest.json').read_text())
 for name,digest in manifest['evidence_sha256'].items():
  require(hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'evidence changed: '+name)
 identity=json.loads((root/'sdk-identity.json').read_text())
 require(identity['sdk_git_head']=='81d81fba72c42d348c589514c774bbfe01e110fa','SDK revision mismatch')
 require(identity['reverse_patch_check_returncode']==0,'SDK patch absent')
 require(identity['sdk_object_mtime_ns']>=identity['sdk_cpp_mtime_ns'] and identity['installed_bgfx_mtime_ns']>=identity['sdk_object_mtime_ns'],'installed SDK predates patched source')
 patch=(root/'bgfx-egl-swap-interval-before-prime.patch').read_text()
 require('EGL_CHECK(eglSwapInterval(m_display, _swapInterval) );' in patch and 'm_appliedSwapInterval = _swapInterval;' in patch,'initial interval fix absent')
 with (root/'historical-stress.csv').open() as f:rows=list(csv.DictReader(f))
 counts=collections.Counter((x['variant'],x['status']) for x in rows)
 expected={('baseline-cache-on','FAIL'):2,('baseline-cache-off','FAIL'):1,('track-per-surface','FAIL'):1,('track-plus-resize','FAIL'):1,('trace-before-prime','FAIL'):1,('interval-before-prime-instrumented','PASS'):20,('interval-before-prime-clean','PASS'):8,('interval-before-prime-clean-cache-off','PASS'):4}
 require(counts==expected and len(rows)==38,'historical campaign changed')
 for row in rows:
  require(row['hardware_gpu']=='True','hardware GPU proof missing')
  if row['status']=='PASS':require(row['visible_changes']=='100' and row['regenerated_changes']=='100','spline lifecycle not verified')
 stack=(root/'historical-hang-stack.txt').read_text();trace=(root/'historical-hang-trace.txt').read_text()
 require('loader_dri3_set_swap_interval' in stack and 'xcb_wait_for_special_event' in stack and 'bgfx::Context::frame' in stack,'hang stack not present')
 require('BGFX_EGL_INTERVAL begin=makeCurrent value=0' in trace and 'BGFX_EGL_INTERVAL done=makeCurrent' not in trace,'EGL interval trace absent')
 current=(root/'current-ctest.log').read_text();detail=(root/'current-ctest-detail.log').read_text()
 names=['CoinBgfxWindowOpenGLTest','CoinBgfxMultiWindowOpenGLTest','CoinBgfxMultiWindowNativePixels_openglTest','CoinBgfxMultiWindowOffscreenFirstOpenGLTest','CoinBgfxMultiWindow_weighted_oit_openglTest','CoinBgfxMultiWindow_sorted_layers_openglTest']
 require(all(re.search(re.escape(name)+r'.*Passed',current) for name in names) and '100% tests passed, 0 tests failed out of 6' in current,'current window tests incomplete')
 require(detail.count('Test Passed.')==6 and 'multiwindow_x11_pixels=required' in detail and 'expected=0' in detail and 'expected=1' in detail,'native pixels were not checked')
 result={'B05_xwayland_amd_linux_closed':True,'cause':'EGL swap interval after initial secondary-surface swaps','program_cache_required_for_failure':False,'historical_timeouts_with_cache_on_and_off':6,'historical_post_fix_spline_passes':32,'current_window_tests_passed':6,'installed_sdk_patch_verified':True,'current_freecad_consumer_rerun':False,'source_and_evidence_hashes_verified':True}
 (root/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
