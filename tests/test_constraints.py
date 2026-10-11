"""Exercise constraint resource extraction across independent C files."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import jsonschema


ROOT = Path(__file__).resolve().parents[1]


class ConstraintResourcesTest(unittest.TestCase):
    def test_constraint_parts_and_cross_unit_resources(self):
        header = r'''
typedef enum { ATTACH_NONE, ATTACH_FORM = 3 } Attachment;
typedef struct { Attachment attachment; } ConstraintRec;
typedef struct {
    char *name, *resource_class, *resource_type;
    unsigned long size, offset;
} Resource;
typedef struct CoreClass {
    struct CoreClass *superclass;
    char *class_name;
    Resource *resources;
} CoreClass;
typedef CoreClass *WidgetClass;
extern Resource constraints[];
'''
        source = '#include "shared.h"\n'
        for part, member in (("constraint_part", "constraints"),
                             ("constraint_class", "resources"),
                             ("constraint", "resources")):
            source += f'''
struct {{ CoreClass core; struct {{ Resource *{member}; }} {part}; }}
    {part}Rec = {{{{0, "{part}", 0}}, {{constraints}}}};
WidgetClass {part}WidgetClass = &{part}Rec.core;
'''
        source += r'''
struct { CoreClass core; struct { Resource *constraints; } constraint_part; }
    emptyRec = {{0, "Empty", 0}, {0}};
WidgetClass emptyWidgetClass = &emptyRec.core;
struct { CoreClass core; } plainRec = {{0, "Plain", 0}};
WidgetClass plainWidgetClass = &plainRec.core;
'''
        array_source = r'''
#include "shared.h"
Resource constraints[] = {
    {"attachment", "Attachment", "Attachment", sizeof(Attachment),
     __builtin_offsetof(ConstraintRec, attachment)},
    {"padding", "Padding", "Short", sizeof(short), 0}
};
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "shared.h").write_text(header)
            (path / "widgets.c").write_text(source)
            (path / "arrays.c").write_text(array_source)
            subprocess.run(
                [sys.executable, str(ROOT / "src/mkmir.py"), str(path),
                 "-o", str(path / "mir.json")],
                check=True, capture_output=True, text=True,
            )
            artifact = json.loads((path / "mir.json").read_text())
        expected = {
            "attachment": {
                "ctype": "Attachment", "class": "Attachment",
                "rtype": "Attachment", "set": True, "get": True,
            },
            "padding": {
                "ctype": "short", "class": "Padding", "rtype": "Short",
                "set": True, "get": True,
            },
        }
        for part in ("constraint_part", "constraint_class", "constraint"):
            widget = artifact["widgets"][part + "WidgetClass"]
            self.assertEqual(widget["constraints"], expected)
            self.assertEqual(widget["resources"], {})
        self.assertEqual(artifact["widgets"]["emptyWidgetClass"]["constraints"], {})
        self.assertNotIn("constraints", artifact["widgets"]["plainWidgetClass"])
        self.assertEqual(artifact["types"]["Attachment"], {
            "type": "enum", "fields": {"ATTACH_NONE": 0, "ATTACH_FORM": 3},
        })
        jsonschema.validate(artifact, json.loads(
            (ROOT / "schema/mir.schema.json").read_text()))


if __name__ == "__main__":
    unittest.main()
