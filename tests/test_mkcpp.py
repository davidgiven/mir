"""Check widget inheritance in the generated C++ boilerplate."""

import importlib.util
from pathlib import Path
import unittest
from graphlib import CycleError


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("mkcpp", ROOT / "src/mkcpp.py")
mkcpp = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mkcpp)


class WidgetClassesTest(unittest.TestCase):
    def test_superclasses_precede_subclasses(self):
        header = mkcpp.generate_header({"widgets": {
            "aLeaf": {"name": "Leaf", "superclass": "zMiddle"},
            "zMiddle": {"name": "Middle", "superclass": "zRoot"},
            "zRoot": {"name": "Root"},
            "aliasRoot": {"name": "Root"},
            "missing": {"name": "Missing", "superclass": "absent"},
        }})
        self.assertIn("class CRoot : public CWidget {};", header)
        self.assertIn("class CMiddle : public CRoot {};", header)
        self.assertIn("class CLeaf : public CMiddle {};", header)
        self.assertIn("class CMissing : public CWidget {};", header)
        self.assertLess(header.index("class CRoot"), header.index("class CMiddle"))
        self.assertLess(header.index("class CMiddle"), header.index("class CLeaf"))
        self.assertEqual(header.count("class CRoot"), 1)

    def test_cycles_are_rejected(self):
        with self.assertRaises(CycleError):
            mkcpp.generate_header({"widgets": {
                "first": {"name": "First", "superclass": "second"},
                "second": {"name": "Second", "superclass": "first"},
            }})


if __name__ == "__main__":
    unittest.main()
