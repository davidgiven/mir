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
    def generate_header(self, artifact):
        for widget in artifact["widgets"].values():
            widget.setdefault("header", f"Xm/{widget['name']}.h")
        return mkcpp.generate_header(artifact)

    def test_superclasses_precede_subclasses(self):
        header = self.generate_header({"widgets": {
            "aLeaf": {"name": "Leaf", "superclass": "zMiddle"},
            "zMiddle": {"name": "Middle", "superclass": "zRoot"},
            "zRoot": {"name": "Root"},
            "aliasRoot": {"name": "Root"},
            "missing": {"name": "Missing", "superclass": "absent"},
        }})
        self.assertIn("class CRootImpl : public CWidgetImpl<Derived>", header)
        self.assertIn("class CMiddleImpl : public CRootImpl<Derived>", header)
        self.assertIn("class CLeafImpl : public CMiddleImpl<Derived>", header)
        self.assertIn("class CMissingImpl : public CWidgetImpl<Derived>", header)
        self.assertLess(header.index("class CRootImpl"), header.index("class CMiddleImpl"))
        self.assertLess(header.index("class CMiddleImpl"), header.index("class CLeafImpl"))
        self.assertEqual(header.count("class CRootImpl"), 1)

    def test_setters_use_owned_resources_and_declared_types(self):
        header = self.generate_header({"widgets": {
            "root": {"name": "Root", "resources": {
                "value": {"ctype": "int"},
            }},
            "child": {"name": "Child", "superclass": "root", "resources": {
                "value": {"ctype": "char *"},
                "items": {"ctype": "XmString[3]"},
                "pri.vate": {"ctype": "Boolean"},
                "unresolved": {},
            }},
        }})
        root = header.split("class CRootImpl", 1)[1].split("class CRoot :", 1)[0]
        child = header.split("class CChildImpl", 1)[1].split("class CChild :", 1)[0]
        self.assertIn("Derived& setValue(int value)", root)
        self.assertNotIn("setItems", root)
        self.assertIn("Derived& setValue(char * value)", child)
        self.assertNotIn("setValue(int value)", child)
        self.assertIn("Derived& setItems(XmString value[3])", child)
        self.assertIn("Derived& setPri_vate(Boolean value)", child)
        self.assertNotIn("setUnresolved", child)
        self.assertIn("return static_cast<Derived&>(*this);", child)
        self.assertIn('XtVaSetValues(this->_widget, "value", value, nullptr);', child)
        self.assertIn('XtVaSetValues(this->_widget, "pri.vate", value, nullptr);', child)

    def test_cycles_are_rejected(self):
        with self.assertRaises(CycleError):
            self.generate_header({"widgets": {
                "first": {"name": "First", "superclass": "second"},
                "second": {"name": "Second", "superclass": "first"},
            }})


if __name__ == "__main__":
    unittest.main()
