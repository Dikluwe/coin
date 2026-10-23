#!/usr/bin/env python3
"""Deterministic guards for CoinRender CPU hot paths.

These tests deliberately inspect only narrow function bodies.  They complement
functional tests: their purpose is to keep accidental linear searches, repeated
whole-plan comparisons, and duplicate validation out of per-frame loops where
output equality alone cannot reveal the regression.
"""
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def source(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def body(relative, signature):
    text = source(relative)
    start = text.find(signature)
    if start < 0:
        raise AssertionError(f"missing function signature {signature!r} in {relative}")
    opening = text.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing function body for {signature!r}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[opening : pos + 1]
    raise AssertionError(f"unterminated function body for {signature!r}")


class CoinRenderHotPathSourceGuards(unittest.TestCase):
    def test_polygon_boundary_does_not_linearly_cancel_and_erase_edges(self):
        text = body(
            "src/rendering/coinrender/CoinRenderPolygonStyleCore.cpp",
            "coin_render_resolve_styled_triangle_boundaries(",
        )
        self.assertNotRegex(text, r"std::find_if\s*\(\s*boundary\.begin\(\)")
        self.assertNotRegex(text, r"boundary\.erase\s*\(")

    def test_bgfx_shadow_pass_does_not_search_caster_vector_per_draw(self):
        text = source("src/rendering/coinbgfx/CoinBgfxBackend.cpp")
        offenders = re.findall(
            r"std::find\s*\(\s*pass\.casterDraws\.begin\(\)\s*,\s*"
            r"pass\.casterDraws\.end\(\)\s*,\s*draw\.sourceDrawSlot\s*\)",
            text,
        )
        self.assertEqual(offenders, [], "shadow submission still performs O(draws*casters) membership")

    def test_texture2_capture_has_frame_local_source_cache(self):
        header = source("src/rendering/coinrender/CoinRenderFramePlanBuilder.h")
        capture = body(
            "src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp",
            "CoinRenderFramePlanBuilder::captureTextureUnit(",
        )
        cache_declarations = re.findall(
            r"(?:unordered_map|SbHash)<[^;]+>\s+([A-Za-z0-9_]*captured[A-Za-z0-9_]*Texture[A-Za-z0-9_]*)",
            header,
            re.IGNORECASE,
        )
        self.assertTrue(cache_declarations, "ordinary 2D texture capture has no frame-local source cache")
        self.assertTrue(
            any(name in capture for name in cache_declarations),
            "ordinary 2D texture cache is declared but unused by captureTextureUnit",
        )
        self.assertRegex(capture, r"sourceRevision|revision", "revisioned sources must key safe reuse")
        self.assertRegex(capture, r"memcmp|Digest|digest|pixels", "unrevisioned bytes must still be observed")

    def test_depth_resolution_does_not_allocate_vectors_per_triangle(self):
        text = body(
            "src/rendering/coinrender/CoinRenderDepthCore.cpp",
            "coin_render_resolve_triangle_depth(",
        )
        loop = text.find("for (uint32_t offset")
        self.assertGreaterEqual(loop, 0)
        hot = text[loop:]
        self.assertNotRegex(hot, r"std::vector<[^>]+>\s+(ring|clipped|window)\s*;")

    def test_reuse_classification_uses_one_structural_walk(self):
        text = body(
            "src/rendering/coinrender/CoinRenderFrameReuseCore.cpp",
            "CoinRenderFrameReuseCore::classify(",
        )
        legacy_walks = sum(name in text for name in (
            "hasSamePayload(", "sameCameraIndependentPayload(", "sameExecutionStructure("
        ))
        self.assertLessEqual(
            legacy_walks, 1,
            "classification still performs several progressively weaker whole-plan comparisons",
        )

    def test_bump_transform_interning_is_not_a_linear_vertex_scan(self):
        text = body(
            "src/rendering/coinrender/CoinRenderFramePlanBuilder.cpp",
            "CoinRenderFramePlanBuilder::addVertex(",
        )
        self.assertNotRegex(
            text,
            r"for\s*\([^)]*currentPlan\.bumpTransforms\.size\(\)",
            "each bumped vertex still scans every prior transform",
        )

    def test_target_does_not_validate_extended_bump_then_whole_plan_again(self):
        text = body(
            "src/rendering/coinrender/CoinRenderTarget.cpp",
            "CoinRenderTargetP::validateProfileInternal(",
        )
        bump = text.find("coin_render_validate_extended_bump")
        whole = text.find("frame.isValid")
        self.assertFalse(
            bump >= 0 and whole > bump,
            "target validates the complete plan inside extended bump and then again directly",
        )
        public_validator = body(
            "src/rendering/coinrender/CoinRenderBumpMapCore.cpp",
            "coin_render_validate_extended_bump(",
        )
        self.assertRegex(
            public_validator,
            r"isValid|validated|preflight",
            "the public validator must retain a safe standalone validation path",
        )


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(CoinRenderHotPathSourceGuards)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
