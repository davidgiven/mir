"""Exercise API enum discovery using real libclang translation units."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ApiTypesTest(unittest.TestCase):
    def test_referenced_enums(self):
        source = r'''
typedef struct WidgetRec *Widget;
enum Mode { MODE_A, MODE_B };
typedef enum Mode Mode;
typedef Mode Alias;
enum Unused { UNUSED };
typedef int Number;
typedef struct { Alias mode; } WidgetRec;
typedef struct {
    char *name, *resource_class, *resource_type;
    unsigned long size, offset;
} Resource;
Resource resources[] = {
    {"mode", "Mode", "Mode", sizeof(Alias),
     __builtin_offsetof(WidgetRec, mode)},
    {"fallback", "Mode", "Mode", sizeof(enum Mode), 0}
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
enum Unused OtherFunction(void);
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.c").write_text(source)
            subprocess.run(
                [sys.executable, str(ROOT / "src/mkmir.py"), str(path),
                 "-o", str(path / "mir.json")],
                check=True, capture_output=True, text=True,
            )
            artifact = json.loads((path / "mir.json").read_text())
        self.assertEqual(artifact["types"], {
            "Alias": {}, "const Mode": {}, "enum Mode": {},
        })
        self.assertEqual(
            artifact["widgets"]["testWidgetClass"]["resources"]["mode"]["ctype"],
            "Alias",
        )
        import jsonschema
        jsonschema.validate(artifact, json.loads(
            (ROOT / "schema/mir.schema.json").read_text()))


if __name__ == "__main__":
    unittest.main()
