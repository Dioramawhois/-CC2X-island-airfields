"""Keep generated player metadata aligned with the source mod declaration."""
import json
from pathlib import Path
import unittest
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


class ReleaseMetadataTest(unittest.TestCase):
    def test_player_package_matches_mod_declaration(self):
        package = json.loads((ROOT/'package.json').read_text(encoding='utf-8'))
        mod = ET.fromstring((ROOT/'mod.xml').read_bytes())
        for field in ('name', 'author', 'description', 'version'):
            self.assertEqual(package[field], mod.attrib[field], field)
        self.assertEqual(str(package['id']), mod.find('cc2mce').attrib['id'])
