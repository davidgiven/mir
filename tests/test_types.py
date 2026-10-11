"""Exercise API type discovery using real libclang translation units."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ApiTypesTest(unittest.TestCase):
    def test_referenced_types(self):
        source = r'''
typedef struct WidgetRec *Widget;
enum Mode { MODE_A = -3, MODE_B, MODE_C = 1 << 5, MODE_D = MODE_C, MODE_E };
typedef enum Mode Mode;
typedef Mode Alias;
typedef enum { ANON_A = 7, ANON_B } Anonymous;
enum { XmFirst = -1, XtSecond, OtherConstant = 5, XmSameValue = 7 };
enum Named { XmNamed = 7 };
typedef enum { XtTypedef = 8 } NamedTypedef;
enum { XmLaterNamed = 9 } enum_variable;
typedef __typeof__(enum_variable) LaterNamed;
enum External;
enum Unused { UNUSED };
typedef int Number;
typedef unsigned long Cardinal;
typedef Cardinal Count;
typedef unsigned char Byte;
typedef _Bool Flag;
typedef double Real;
typedef int *Pointer;
typedef int Array[2];
typedef short UnusedInteger;
typedef struct { Alias mode; Count count; } WidgetRec;
typedef struct {
    char *name, *resource_class, *resource_type;
    unsigned long size, offset;
} Resource;
Resource resources[] = {
    {"mode", "Mode", "Mode", sizeof(Alias),
     __builtin_offsetof(WidgetRec, mode)},
    {"fallback", "Mode", "Mode", sizeof(enum Mode), 0},
    {"count", "Count", "Count", sizeof(Count),
     __builtin_offsetof(WidgetRec, count)},
    {"byte", "Byte", "Byte", sizeof(Byte), 0}
};
typedef struct CoreClass {
    struct CoreClass *superclass;
    char *class_name;
    Resource *resources;
} CoreClass;
typedef CoreClass *WidgetClass;
struct { CoreClass core; } testClassRec = {
    {0, "Test", resources}
};
WidgetClass testWidgetClass = &testClassRec.core;
Alias TestGet(Widget widget);
void TestSet(Widget widget, const Mode mode, Number number, Mode *pointer);
enum Mode TestMode(Widget widget);
Anonymous TestAnonymous(Widget widget);
enum External TestExternal(Widget widget);
Count TestCount(Widget widget, const Count count, Flag flag, Real real,
               Pointer pointer, Array array, int bare);
enum Unused OtherFunction(void);
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(source)
            (path / "external.c").write_text(
                "enum External { EXTERNAL_A = 42 };\n"
                "enum { XtExternal = 42 };\n"
            )
            subprocess.run(
                [sys.executable, str(ROOT / "src/mkmir.py"), str(path),
                 "-o", str(path / "mir.json")],
                check=True, capture_output=True, text=True,
            )
            artifact = json.loads((path / "mir.json").read_text())
        expected_enum = {
            "type": "enum",
            "fields": {
                "MODE_A": -3, "MODE_B": -2, "MODE_C": 32,
                "MODE_D": 32, "MODE_E": 33,
            },
        }
        expected_types = {
            name: expected_enum for name in ("Alias", "const Mode", "enum Mode")
        }
        expected_types["Anonymous"] = {
            "type": "enum", "fields": {"ANON_A": 7, "ANON_B": 8},
        }
        expected_types["enum External"] = {
            "type": "enum", "fields": {"EXTERNAL_A": 42},
        }
        expected_types["XtEnum"] = {
            "type": "enum",
            "fields": {
                "XmFirst": -1, "XtSecond": 0, "XmSameValue": 7,
                "XtExternal": 42,
            },
        }
        expected_types.update({
            name: {"type": "integer", "ctype": ctype}
            for name, ctype in (
                ("Number", "int"), ("Count", "unsigned long"),
                ("const Count", "unsigned long"), ("Byte", "unsigned char"),
                ("Flag", "_Bool"),
            )
        })
        self.assertEqual(artifact["types"], expected_types)
        self.assertEqual(
            artifact["widgets"]["testWidgetClass"]["resources"]["mode"]["ctype"],
            "Alias",
        )
        import jsonschema
        jsonschema.validate(artifact, json.loads(
            (ROOT / "schema/mir.schema.json").read_text()))


if __name__ == "__main__":
    unittest.main()
