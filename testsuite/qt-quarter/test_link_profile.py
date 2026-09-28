"""Link selection uses only an owned profile, without dock click interception."""
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

from run import prepare_profile


class LinkProfile(unittest.TestCase):
    def test_link_fixture_initializes_overlay_preference_before_startup(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = Path(temporary) / 'profile'
            prepare_profile(profile, 'freecad-mouse-links')
            root = ET.parse(profile / 'user.cfg').getroot()
            flag = root.find("./FCParamGroup[@Name='Root']/FCParamGroup[@Name='BaseApp']/"
                             "FCParamGroup[@Name='Preferences']/FCParamGroup[@Name='DockWindows']/"
                             "FCBool[@Name='ActivateOverlay']")
            self.assertIsNotNone(flag)
            self.assertEqual(flag.get('Value'), '0')

    def test_other_cases_keep_production_overlay_defaults(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = Path(temporary) / 'profile'
            prepare_profile(profile, 'freecad-overlays')
            self.assertFalse((profile / 'user.cfg').exists())

    def test_existing_profile_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            profile = Path(temporary)
            user = profile / 'user.cfg'
            user.write_text('preserve existing parameters')
            prepare_profile(profile, 'freecad-mouse-links')
            self.assertEqual(user.read_text(), 'preserve existing parameters')
