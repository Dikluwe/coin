"""Adversarial controls for the real FreeCAD consumer observations."""
import math
import json
import unittest
from types import SimpleNamespace
from unittest.mock import Mock
from run import classify

from host_consumer_oracles import (require_content, require_mutation,
                                   require_cleanup, require_pointer_identity,
                                   require_drag, require_idle, require_dpr,
                                   require_interactive_mesh, require_single_coin,
                                   loaded_coin_images, require_host_hover, require_host_selection,
                                   canonical_topology_identity, require_released_draggers,
                                   NativePublicationQuietWindow)


class HostConsumerOracles(unittest.TestCase):
    @staticmethod
    def publication(**changes):
        return dict({'viewer_alive': True, 'status': 0, 'frames': 10, 'serial': 20,
                     'fallback_reason': '', 'opengl_viewport': False}, **changes)

    def test_publication_wait_cannot_accept_no_progress(self):
        wait = NativePublicationQuietWindow(self.publication())
        self.assertFalse(wait.observe(self.publication(), 0))
        self.assertFalse(wait.observe(self.publication(), 120000))
        self.assertIsNone(wait.quiet_started_ms)

    def test_publication_wait_needs_both_counters_to_advance(self):
        for state in (self.publication(frames=11), self.publication(serial=21)):
            with self.subTest(state=state):
                wait = NativePublicationQuietWindow(self.publication())
                self.assertFalse(wait.observe(state, 0))
                self.assertFalse(wait.observe(state, 1000))

    def test_publication_wait_rejects_invalid_observation(self):
        for changes in ({'status': 1}, {'status': False}, {'viewer_alive': False},
                        {'frames': True}, {'serial': None}, {'fallback_reason': None}):
            with self.subTest(changes=changes):
                wait = NativePublicationQuietWindow(self.publication())
                state = self.publication(frames=11, serial=21)
                state.update(changes)
                self.assertFalse(wait.observe(state, 0))
                self.assertFalse(wait.observe(state, 1000))

    def test_publication_wait_new_submission_restarts_the_quiet_window(self):
        wait = NativePublicationQuietWindow(self.publication())
        first = self.publication(frames=11, serial=21)
        second = self.publication(frames=12, serial=22)
        self.assertFalse(wait.observe(first, 0))
        self.assertFalse(wait.observe(first, 599))
        self.assertFalse(wait.observe(second, 600))
        self.assertFalse(wait.observe(second, 1199))
        self.assertTrue(wait.observe(second, 1200))

    def test_publication_wait_accepts_legitimate_progress_after_quiet_time(self):
        wait = NativePublicationQuietWindow(self.publication())
        state = self.publication(frames=11, serial=21)
        self.assertFalse(wait.observe(state, 4218))
        self.assertFalse(wait.observe(state, 4817))
        self.assertTrue(wait.observe(state, 4818))

    def test_publication_wait_never_accepts_gl_fallback(self):
        for changes in ({'opengl_viewport': True}, {'fallback_reason': 'legacy fallback'}):
            wait = NativePublicationQuietWindow(self.publication())
            state = self.publication(frames=11, serial=21)
            state.update(changes)
            with self.subTest(changes=changes), self.assertRaises(RuntimeError):
                wait.observe(state, 1000)

    def test_released_input_cannot_leave_an_active_or_pending_dragger(self):
        observer = {'viewer_alive': True, 'manager_present': True, 'action_present': True}
        require_released_draggers(dict(observer, scene_active_dragger_count=0, grabber_present=False))
        # A DOWN without Motion can be active before any grabber is installed.
        for state in ({'scene_active_dragger_count': 1, 'grabber_present': False},
                      {'scene_active_dragger_count': 0, 'grabber_present': True},
                      {'scene_active_dragger_count': 1, 'grabber_present': True},
                      {'scene_active_dragger_count': False, 'grabber_present': False},
                      {'scene_active_dragger_count': 0}, {'grabber_present': False}, {}):
            with self.subTest(state=state), self.assertRaises(RuntimeError):
                require_released_draggers(dict(observer, **state))
        with self.assertRaises(RuntimeError):
            require_released_draggers({'scene_active_dragger_count': 0, 'grabber_present': False})

    def test_consumer_success_requires_normal_teardown_and_real_exit_zero(self):
        result = {'test': 'freecad-host-consumer', 'status': 'REFERENCE_PASS',
                  'reference_gl': True, 'shutdown': {'mode': 'normal-host-close',
                  'saved_documents': 1, 'documents_closed': True,
                  'probes_removed': True, 'filters_removed': True}}
        def status(value, code=0):
            return classify(code, 'RESULT ' + json.dumps(value), require_bgfx=False)['status']
        self.assertEqual(status(result), 'REFERENCE_PASS')
        self.assertEqual(status(result, -9), 'FAIL')
        self.assertEqual(status(result, 1), 'FAIL')
        self.assertEqual(status(dict(result, shutdown={})), 'FAIL')
        for key, invalid in (('mode', 'immediate-exit'), ('saved_documents', 0),
                             ('saved_documents', True), ('documents_closed', False),
                             ('probes_removed', False), ('filters_removed', False)):
            with self.subTest(key=key, invalid=invalid):
                shutdown = dict(result['shutdown'], **{key: invalid})
                self.assertEqual(status(dict(result, shutdown=shutdown)), 'FAIL')

    def test_topology_resolution_preserves_owner_and_full_occurrence(self):
        mapped = 'Consumer.;#14:1;:G0;XTR;:Hcb8:8,F.Face6'
        resolver = Mock(return_value=(object(), mapped, 'Consumer.Face6'))
        document = SimpleNamespace(getObject=Mock(return_value=SimpleNamespace(resolveSubElement=resolver)))
        self.assertEqual(canonical_topology_identity(document, ('Body', mapped)),
                         ('Body', 'Consumer.Face6'))
        resolver.assert_called_once_with(mapped, True, 0)
        document.getObject.assert_called_once_with('Body')
        resolver.return_value = (object(), mapped, 'Instance.Consumer.Face6')
        self.assertEqual(canonical_topology_identity(document, ('Link', mapped)),
                         ('Link', 'Instance.Consumer.Face6'))
        resolver.return_value = (None, '', '')
        with self.assertRaisesRegex(RuntimeError, 'cannot be resolved'):
            canonical_topology_identity(document, ('Body', mapped))

    def test_host_without_visual_hover_still_preserves_its_pixel_policy(self):
        require_host_hover(0, False)
        require_host_hover(20, True)
        with self.assertRaises(RuntimeError):
            require_host_hover(20, False)
        with self.assertRaises(RuntimeError):
            require_host_hover(0, True)

    def test_one_coin_is_counted_by_image_not_mapping_or_link_name(self):
        maps = ('1000-2000 r--p 00000000 00:15 123 /prefix/lib/libCoin.so.80\n'
                '2000-3000 r-xp 00001000 00:15 123 /prefix/lib/libCoin.so.80\n'
                '3000-4000 r--p 00000000 00:15 123 /prefix/lib/libCoin.so.80c\n'
                '4000-5000 r-xp 00000000 00:15 456 /prefix/lib/libCoinRender.so.1\n')
        self.assertEqual(len(require_single_coin(maps)), 1)
        self.assertEqual(require_single_coin(maps)[0]['inode'], 123)

    def test_host_without_visual_selection_still_preserves_its_pixel_policy(self):
        require_host_selection(0, False)
        require_host_selection(20, True)
        with self.assertRaises(RuntimeError):
            require_host_selection(20, False)
        with self.assertRaises(RuntimeError):
            require_host_selection(0, True)

    def test_distro_coin_and_current_coin_cannot_share_host_types(self):
        maps = ('1000-2000 r--p 00000000 00:15 123 /prefix/lib/libCoin.so.80\n'
                '2000-3000 r-xp 00000000 00:15 456 /usr/lib/libCoin.so.80c\n')
        self.assertEqual(len(loaded_coin_images(maps)), 2)
        with self.assertRaisesRegex(RuntimeError, 'found 2'):
            require_single_coin(maps)
        with self.assertRaisesRegex(RuntimeError, 'found 0'):
            require_single_coin('1000-2000 r--p 00000000 00:15 456 /prefix/lib/libCoinRender.so.1')

    def test_replaced_library_with_same_path_is_still_two_loaded_images(self):
        maps = ('1000-2000 r--p 00000000 00:15 123 /prefix/lib/libCoin.so.80 (deleted)\n'
                '2000-3000 r-xp 00000000 00:15 456 /prefix/lib/libCoin.so.80\n')
        with self.assertRaisesRegex(RuntimeError, 'found 2'):
            require_single_coin(maps)

    def test_label_background_cannot_replace_glyphs(self):
        require_content(20, 20)
        with self.assertRaisesRegex(RuntimeError, 'glyphs missing'):
            require_content(10000, 0)
        with self.assertRaisesRegex(RuntimeError, 'content missing'):
            require_content(0, 10000)

    def test_update_and_removal_are_separate_observations(self):
        require_mutation(30)
        require_cleanup(30)
        with self.assertRaises(RuntimeError):
            require_mutation(0)
        with self.assertRaises(RuntimeError):
            require_cleanup(31)

    def test_wrong_object_and_wrong_subelement_do_not_count_as_input(self):
        target = ('Consumer', 'Constraint1')
        require_pointer_identity(target, target)
        for wrong in (None, ('Box', 'Constraint1'), ('Consumer', 'Edge1'),
                      ('Consumer', ''), ()):
            with self.subTest(wrong=wrong), self.assertRaises(RuntimeError):
                require_pointer_identity(wrong, target)
        with self.assertRaises(RuntimeError):
            require_pointer_identity(None, None)

    def test_drag_requires_finite_motion_and_native_button_pair(self):
        require_drag((0, 0, 0), (1, 0, 0), 1, 2)
        for before, after, motions, buttons in (
                ((0, 0, 0), (0, 0, 0), 10, 2),
                ((0, 0, 0), (1, 0, 0), 0, 2),
                ((0, 0, 0), (1, 0, 0), 1, 1),
                ((0, 0, 0), (math.nan, 0, 0), 1, 2),
                ((math.inf, 0, 0), (1, 0, 0), 1, 2),
                ((0, 0), (1, 0, 0), 1, 2)):
            with self.subTest(after=after, motions=motions, buttons=buttons):
                with self.assertRaises(RuntimeError):
                    require_drag(before, after, motions, buttons)

    def test_drag_requires_the_actual_dragger_lifecycle(self):
        valid = {'start': 1, 'motion': 1, 'finish': 1}
        require_drag((0,0,0), (1,0,0), 1, 2, valid)
        for phase in valid:
            with self.subTest(phase=phase), self.assertRaisesRegex(RuntimeError, 'callback lifecycle'):
                require_drag((0,0,0), (1,0,0), 1, 2, dict(valid, **{phase: 0}))

    def test_idle_requires_no_new_native_frames(self):
        require_idle(11, 11)
        with self.assertRaises(RuntimeError):
            require_idle(11, 12)

    def test_dpr_one_is_not_evidence_of_dpr_two(self):
        require_dpr(1.0, 1.0)
        require_dpr(2.0, 2.0)
        for actual, requested in ((1, 2), (2, 1), (1.5, 1.5), (0, 1)):
            with self.subTest(actual=actual, requested=requested):
                with self.assertRaises(RuntimeError):
                    require_dpr(actual, requested)

    def test_mesh_release_image_cannot_replace_active_pointer_sample(self):
        valid = dict(facets=80, limit=10, pressed=True, camera_changed=True,
                     active_pixels=243, released_pixels=2000,
                     projected_samples=27, sample_hits=27)
        require_interactive_mesh(**valid)
        for key, value in (('pressed', False), ('camera_changed', False),
                           ('facets', 10), ('limit', 0), ('active_pixels', 2000),
                           ('active_pixels', 0), ('released_pixels', 243),
                           ('projected_samples', 0), ('sample_hits', 26)):
            with self.subTest(key=key, value=value):
                with self.assertRaises(RuntimeError):
                    require_interactive_mesh(**dict(valid, **{key: value}))


if __name__ == '__main__':
    unittest.main()
