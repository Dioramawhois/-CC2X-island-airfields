"""A package build must never delete an existing mod folder."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

NATIVE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(NATIVE))
spec = importlib.util.spec_from_file_location('island_package', NATIVE / 'package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
sys.path.pop(0)


class PackagingSafety(unittest.TestCase):
    def test_existing_output_is_preserved_before_any_build_or_sdk_access(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'Island Airfields'
            output.mkdir()
            marker = output / 'player-file.txt'
            marker.write_bytes(b'keep this existing mod')
            with patch.object(sys, 'argv', ['package.py', '--luac', 'unused', '--output', str(output)]), \
                    patch.object(package, 'sdk_path', side_effect=AssertionError('SDK accessed')):
                with self.assertRaisesRegex(ValueError, 'Existing output'):
                    package.main()
            self.assertEqual(marker.read_bytes(), b'keep this existing mod')


if __name__ == '__main__':
    unittest.main()
