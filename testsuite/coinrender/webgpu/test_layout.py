#!/usr/bin/env python3
"""Geometry checks for the HTTPS demo; requires Python Playwright and trusted TLS.

Example: python test_layout.py --url https://host:8443/ --channel chrome -v
Use COIN_WEBGPU_BROWSER_ARGS for a JSON array of machine-specific browser flags.
The runner does not bypass certificate validation or secure-context requirements.
"""
import argparse
import json
import os
import unittest
from urllib.parse import urlsplit

OPTIONS = None
VIEWPORTS = ((640, 480), (390, 844), (1920, 1080), (1280, 720), (3840, 2160))

GEOMETRY = r"""() => {
  const box = element => {
    const r = element.getBoundingClientRect();
    return {x:r.x, y:r.y, width:r.width, height:r.height,
      right:r.right, bottom:r.bottom};
  };
  const visible = element => {
    const original = element.getBoundingClientRect();
    let left = Math.max(0, original.left), top = Math.max(0, original.top);
    let right = Math.min(innerWidth, original.right);
    let bottom = Math.min(innerHeight, original.bottom);
    for (let ancestor = element.parentElement; ancestor; ancestor = ancestor.parentElement) {
      const style = getComputedStyle(ancestor), rect = ancestor.getBoundingClientRect();
      if (/(hidden|clip|scroll|auto)/.test(style.overflowX)) {
        left = Math.max(left, rect.left); right = Math.min(right, rect.right);
      }
      if (/(hidden|clip|scroll|auto)/.test(style.overflowY)) {
        top = Math.max(top, rect.top); bottom = Math.min(bottom, rect.bottom);
      }
    }
    const style = getComputedStyle(element);
    return {width:Math.max(0,right-left), height:Math.max(0,bottom-top),
      displayed:style.display !== 'none' && style.visibility !== 'hidden' && Number(style.opacity)>0};
  };
  const stage = document.getElementById('presentation-stage');
  const result = {viewport:{width:innerWidth,height:innerHeight},
    document:{width:document.documentElement.scrollWidth,height:document.documentElement.scrollHeight},
    body:{width:document.body.scrollWidth,height:document.body.scrollHeight},
    stage:stage ? box(stage) : null, elements:{}, secure:isSecureContext};
  for (const id of ['render-canvas','status','stats','pause','adapter-info']) {
    const element=document.getElementById(id);
    result.elements[id] = element ? {box:box(element), visible:visible(element),
      inStage:stage?.contains(element), text:element.textContent,
      backing:element instanceof HTMLCanvasElement ? [element.width,element.height] : null} : null;
  }
  return result;
}"""


class PresentationLayout(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from playwright.sync_api import sync_playwright
        cls.driver = sync_playwright().start()
        cls.browser = cls.driver.chromium.launch(
            channel=OPTIONS.channel, headless=not OPTIONS.headed,
            args=json.loads(os.environ.get('COIN_WEBGPU_BROWSER_ARGS', '[]')))

    @classmethod
    def tearDownClass(cls):
        cls.browser.close()
        cls.driver.stop()

    def load(self, width, height, missing_gpu=False):
        context = self.browser.new_context(viewport={'width':width,'height':height})
        self.addCleanup(context.close)
        if missing_gpu:
            context.add_init_script("Object.defineProperty(navigator, 'gpu', {get:()=>undefined});")
        page = context.new_page()
        page.goto(OPTIONS.url, wait_until='domcontentloaded')
        page.wait_for_selector('#presentation-stage', state='attached')
        page.wait_for_function("['error','rendering'].includes(window.coinRenderWebGPU?.status)",
                               timeout=20000)
        if missing_gpu:
            self.assertEqual(page.evaluate('window.coinRenderWebGPU.status'), 'error')
        page.evaluate('() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))')
        return page

    def assert_layout(self, page, allow_long_text=False):
        geometry = page.evaluate(GEOMETRY)
        self.assertTrue(geometry['secure'], 'Demo must be served in a trusted secure context')
        viewport = geometry['viewport']
        width, height = viewport['width'], viewport['height']
        stage = geometry['stage']
        self.assertIsNotNone(stage, geometry)
        expected_width = min(width, height * 16 / 9)
        expected_height = expected_width * 9 / 16
        self.assertAlmostEqual(stage['width'], expected_width, delta=2, msg=geometry)
        self.assertAlmostEqual(stage['height'], expected_height, delta=2, msg=geometry)
        self.assertAlmostEqual(stage['x'], (width - stage['width']) / 2, delta=2, msg=geometry)
        self.assertAlmostEqual(stage['y'], (height - stage['height']) / 2, delta=2, msg=geometry)
        self.assertAlmostEqual(stage['width'] / stage['height'], 16 / 9,
                               delta=0.005, msg=geometry)
        for root in ('document', 'body'):
            self.assertLessEqual(geometry[root]['width'], width + 1, geometry)
            self.assertLessEqual(geometry[root]['height'], height + 1, geometry)
        for name, element in geometry['elements'].items():
            with self.subTest(element=name):
                self.assertIsNotNone(element, geometry)
                self.assertTrue(element['inStage'], f'{name} is outside the presentation stage')
                rect, visible = element['box'], element['visible']
                self.assertGreater(rect['width'], 1, element)
                self.assertGreater(rect['height'], 1, element)
                self.assertTrue(visible['displayed'], element)
                self.assertGreater(visible['width'], 1, element)
                self.assertGreater(visible['height'], 1, element)
                # Detail text may scroll within its own area; canvas and pause must fit entirely.
                if not allow_long_text or name in ('render-canvas', 'pause'):
                    self.assertGreaterEqual(rect['x'], stage['x'] - 1, element)
                    self.assertGreaterEqual(rect['y'], stage['y'] - 1, element)
                    self.assertLessEqual(rect['right'], stage['right'] + 1, element)
                    self.assertLessEqual(rect['bottom'], stage['bottom'] + 1, element)
                    self.assertGreaterEqual(visible['width'], rect['width'] - 2, element)
                    self.assertGreaterEqual(visible['height'], rect['height'] - 2, element)
                if name != 'render-canvas':
                    self.assertTrue(element['text'].strip(), element)
        return geometry

    def test_entire_presentation_fits_all_viewports(self):
        for width, height in VIEWPORTS:
            with self.subTest(viewport=(width,height)):
                page = self.load(width,height)
                self.assert_layout(page)

    def test_error_ui_remains_accessible_without_webgpu(self):
        for width, height in VIEWPORTS:
            with self.subTest(viewport=(width,height)):
                page = self.load(width,height, missing_gpu=True)
                self.assert_layout(page)

    def test_long_error_and_adapter_text_do_not_expand_stage(self):
        for width, height in VIEWPORTS:
            with self.subTest(viewport=(width,height)):
                page = self.load(width,height, missing_gpu=True)
                before = self.assert_layout(page)
                page.evaluate("""() => {
                  document.getElementById('status').textContent =
                    'WebGPU unavailable: ' + 'long error detail '.repeat(100) + 'x'.repeat(4096);
                  document.getElementById('adapter-info').textContent =
                    'Adapter details: ' + 'very long adapter description '.repeat(100) + 'y'.repeat(4096);
                }""")
                page.evaluate('() => new Promise(resolve => requestAnimationFrame(resolve))')
                after = self.assert_layout(page, allow_long_text=True)
                for field in ('x','y','width','height'):
                    self.assertAlmostEqual(before['stage'][field], after['stage'][field], delta=1)

    def test_resize_preserves_centered_stage_and_controls(self):
        page = self.load(1920,1080, missing_gpu=True)
        for width, height in VIEWPORTS + ((1920,1080),):
            with self.subTest(viewport=(width,height)):
                page.set_viewport_size({'width':width,'height':height})
                page.evaluate('() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))')
                self.assert_layout(page)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--channel', default='chrome')
    parser.add_argument('--headed', action='store_true')
    OPTIONS, remaining = parser.parse_known_args()
    if urlsplit(OPTIONS.url).scheme != 'https':
        parser.error('--url must use HTTPS with a certificate trusted by the browser')
    unittest.main(argv=[__file__] + remaining)
