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

    def test_callback_adders_keep_typed_setters(self):
        header = self.generate_header({"widgets": {
            "button": {"name": "Button", "resources": {
                "activateCallback": {"rtype": "Callback", "ctype": "XtCallbackList",
                                     "callback_payload": "XmPushButtonCallbackStruct"},
                "destroyCallback": {"rtype": "Callback"},
                "ordinaryCallback": {"rtype": "Pointer", "ctype": "XtPointer"},
            }},
        }})
        self.assertIn("#include <functional>", header)
        for name, payload in (("ActivateCallback", "XmPushButtonCallbackStruct"),
                              ("DestroyCallback", "void")):
            self.assertIn(f"Derived& add{name}(CWidgetImplBase::Callback<{payload}> callback)", header)
        self.assertIn('this->template addCallback<XmPushButtonCallbackStruct>("activateCallback", std::move(callback));', header)
        self.assertIn('this->template addCallback<void>("destroyCallback", std::move(callback));', header)
        self.assertIn("Derived& setActivateCallback(XtCallbackList value)", header)
        self.assertNotIn("addOrdinaryCallback", header)

    def test_builders_include_inherited_resources_and_exclude_callbacks(self):
        header = self.generate_header({"widgets": {
            "rootClass": {"name": "Root", "resources": {
                "width": {"ctype": "int"},
                "value": {"ctype": "int"},
                "destroyCallback": {"ctype": "XtCallbackList", "rtype": "Callback"},
            }},
            "childClass": {"name": "Child", "superclass": "rootClass", "resources": {
                "value": {"ctype": "char *"},
                "items": {"ctype": "XmString[3]"},
                "unresolved": {},
            }},
        }})
        builder = header.split("class CChildBuilderImpl", 1)[1]
        self.assertIn("CWidgetBuilderBase<Derived, CWidgetClassT, Parent, Pairs...>", builder)
        self.assertNotIn("using CChildBuilder", builder)
        self.assertIn("auto setWidth(int value)", builder)
        self.assertIn("auto setValue(char * value)", builder)
        self.assertNotIn("setValue(int value)", builder)
        self.assertIn("auto setItems(XmString value[3])", builder)
        self.assertIn('return this->set("value", value);', builder)
        self.assertNotIn("setDestroyCallback", builder)
        self.assertNotIn("setUnresolved", builder)

    def test_constructor_builders_are_stubs_inheriting_widget_builder(self):
        header = self.generate_header({"widgets": {
            "containerClass": {"name": "XmContainer", "constructors": {
                "XmCreateContainer": {"name": "Container"},
                "XmCreateContainerDialog": {"name": "ContainerDialog"},
            }},
        }})
        self.assertIn("class CXmContainerBuilder : public CXmContainerBuilderImpl<CXmContainerBuilder, CXmContainer, Parent, Pairs...>", header)
        self.assertIn("class CXmContainerDialogBuilder : public CXmContainerBuilderImpl<CXmContainerDialogBuilder, CXmContainer, Parent, Pairs...>", header)
        self.assertNotIn("WidgetClassP", header)
        dialog = header.split("class CXmContainerDialogBuilder", 1)[1]
        self.assertIn("Parent::template ConstraintsBuilder<CXmContainerDialogBuilder<Parent, Pairs...>>(*this)", dialog)
        self.assertNotIn("using CXmContainerBuilderBase", header)
        self.assertIn("CXmContainerDialogBuilder(const Parent&, const char*) -> CXmContainerDialogBuilder<Parent>;", dialog)
        self.assertIn("CXmContainer build()", dialog)
        self.assertIn("auto arglist = this->buildArgList();", dialog)
        self.assertIn("return CXmContainer(XmCreateContainerDialog(this->_parent,", dialog)
        self.assertNotIn("setWidth", dialog)
        self.assertEqual(header.count("class CXmContainerBuilder :"), 1)


if __name__ == "__main__":
    unittest.main()
