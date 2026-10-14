"""Check widget inheritance in the generated C++ boilerplate."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
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

    def test_extractor_preserves_parameter_order_in_json(self):
        source = r'''
typedef void *Widget;
typedef struct CoreClass { struct CoreClass *superclass; char *class_name; }
    CoreClass;
typedef CoreClass *WidgetClass;
struct { CoreClass core; } testRec = {{0, "Test"}};
WidgetClass testWidgetClass = &testRec.core;
void TestOrdered(Widget widget, int z_first, char *a_second);
void TestUnnamed(Widget widget, int, char *arg1);
void TestNoArgs(Widget widget);
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(source)
            subprocess.run([sys.executable, str(ROOT / "src/mkmir.py"),
                            str(path / "test.c"), "-o", str(path / "mir.json")],
                           capture_output=True, text=True, check=True)
            artifact = json.loads((path / "mir.json").read_text())
        method = artifact["widgets"]["testWidgetClass"]["methods"]["TestOrdered"]
        self.assertEqual(method["parameters"], [
            {"name": "z_first", "type": "int"},
            {"name": "a_second", "type": "char *"},
        ])
        self.assertIn("Derived& ordered(int z_first, char * a_second)",
                      self.generate_header(artifact))
        methods = artifact["widgets"]["testWidgetClass"]["methods"]
        self.assertEqual(methods["TestUnnamed"]["parameters"], [
            {"name": "", "type": "int"}, {"name": "arg1", "type": "char *"},
        ])
        self.assertEqual(methods["TestNoArgs"]["parameters"], [])

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

    @unittest.skipUnless(shutil.which("c++"), "C++ compiler required")
    def test_method_wrappers_forward_in_signature_order(self):
        header = self.generate_header({"widgets": {
            "parent": {"name": "ToyRoot", "methods": {
                "ToyGet": {"name": "Get", "parameters": [], "return_type": "int *"},
                "ToySet": {"name": "Set", "return_type": "void", "parameters": [
                    {"name": "values", "type": "int *"},
                    {"name": "count", "type": "int"},
                ]},
                "ToyVisit": {"name": "Visit", "return_type": "int", "parameters": [
                    {"name": "", "type": "int (*)(int)"},
                    {"name": "values", "type": "int[2]"},
                ]},
                "ToyAddItem": {"name": "AddItem", "return_type": "void", "parameters": [
                    {"name": "item", "type": "XmString"},
                    {"name": "pos", "type": "int"},
                ]},
                "ToyCompare": {"name": "Compare", "return_type": "int", "parameters": [
                    {"name": "first", "type": "XmString"},
                    {"name": "second", "type": "XmString"},
                ]},
            }},
            "child": {"name": "ToyChild", "superclass": "parent"},
        }})
        self.assertIn("Derived& set(int * values, int count)", header)
        self.assertIn("ToySet(this->_widget, values, count);", header)
        self.assertNotIn("return ToySet", header)
        self.assertIn("int visit(int (*arg1)(int), int values[2])", header)
        self.assertNotIn('extern "C"', header)
        self.assertNotIn('ToyGet(Widget', header)
        for ctype in ("XmString", "const char*", "std::unique_ptr<CXmString>"):
            self.assertIn(f"Derived& addItem({ctype} item, int pos)", header)
        self.assertEqual(header.count("int compare("), 9)
        self.assertEqual(mkcpp.named_method_parameters({"parameters": [
            {"name": "", "type": "int"},
            {"name": "arg1", "type": "int"},
        ]})[0][0], "arg1_")
        source = r'''
#include <concepts>
#include <cassert>
#include <memory>
#include <string>
using Widget = void*;
struct StringData { std::string value; };
using XmString = StringData*;
static int live_strings;
class CXmString {
    StringData data;
public:
    explicit CXmString(const char* str): data{str} { ++live_strings; }
    ~CXmString() { --live_strings; }
    operator XmString() { return &data; }
};
class CWidgetImplBase {};
template<typename Derived> class CWidgetImpl : public CWidgetImplBase {
public:
    explicit CWidgetImpl(Widget widget): _widget(widget) {}
    void addCallback() {}
    Widget _widget;
};
template<template<typename...> class Derived, typename CWidgetClassT,
         typename Parent, typename... Pairs>
class CWidgetBuilderBase {};
static Widget expected = reinterpret_cast<Widget>(0x1234);
static int value;
static int add_calls;
extern "C" void ToyAddItem(Widget widget, XmString item, int pos) {
    assert(widget == expected);
    assert(item->value == "hello");
    assert(pos == 3);
    assert(live_strings > 0);
    ++add_calls;
}
extern "C" int ToyCompare(Widget widget, XmString first, XmString second) {
    assert(widget == expected);
    return first->value.compare(second->value);
}
extern "C" int *ToyGet(Widget widget) {
    assert(widget == expected);
    return &value;
}
extern "C" void ToySet(Widget widget, int *values, int count) {
    assert(widget == expected);
    assert(count == 2);
    value = values[0] + values[1];
}
extern "C" int ToyVisit(Widget widget, int (*visitor)(int), int values[2]) {
    assert(widget == expected);
    return visitor(values[0]) + visitor(values[1]);
}
#include "generated.h"
int main() {
    CToyRoot root(expected);
    CToyChild child(expected);
    int values[] = {7, 11};
    assert(&root.set(values, 2) == &root);
    static_assert(std::same_as<decltype(child.set(values, 2)), CToyChild&>);
    assert(&child.set(values, 2).set(values, 2) == &child);
    assert(child.set(values, 2).get() == &value);
    assert(root.get() == &value);
    assert(*child.get() == 18);
    assert(child.visit([](int x) { return x * 2; }, values) == 36);
    assert(live_strings == 0);
    assert(&root.addItem("hello", 3).addItem("hello", 3) == &root);
    assert(live_strings == 0);
    auto item = std::make_unique<CXmString>("hello");
    assert(&child.addItem(std::move(item), 3) == &child);
    assert(!item);
    assert(live_strings == 0);
    {
        CXmString raw("hello");
        root.addItem(static_cast<XmString>(raw), 3);
        assert(live_strings == 1);
        assert(root.compare(static_cast<XmString>(raw), "hello") == 0);
        assert(root.compare("hello", std::make_unique<CXmString>("hello")) == 0);
        assert(root.compare(std::make_unique<CXmString>("hello"),
                            static_cast<XmString>(raw)) == 0);
        assert(root.compare("hello", "world") < 0);
        assert(live_strings == 1);
    }
    assert(live_strings == 0);
    assert(add_calls == 4);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "generated.h").write_text(header)
            (path / "test.cc").write_text(source)
            subprocess.run(["c++", "-std=c++20", f"-I{ROOT / 'include'}",
                            str(path / "test.cc"), "-o", str(path / "test"),
                            "-lXm", "-lXt", "-lX11"], check=True)
            subprocess.run([str(path / "test")], check=True)

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

    def test_c_methods_replace_colliding_resource_accessors(self):
        header = self.generate_header({"widgets": {"root": {
            "name": "Root", "resources": {"value": {"ctype": "int"}},
            "methods": {
                "RootGetValue": {"name": "GetValue", "return_type": "int",
                                 "parameters": []},
                "RootSetValue": {"name": "SetValue", "return_type": "void",
                                 "parameters": [{"name": "value", "type": "int"}]},
            },
        }}})
        widget = header.split("class CRootImpl", 1)[1].split("class CRoot :", 1)[0]
        self.assertEqual(widget.count("getValue()"), 1)
        self.assertEqual(widget.count("setValue(int value)"), 1)
        self.assertIn("Derived& setValue", widget)
        self.assertIn("return RootGetValue(this->_widget);", widget)
        self.assertIn("RootSetValue(this->_widget, value);", widget)
        self.assertNotIn("return RootSetValue", widget)

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
