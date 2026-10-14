"""Infer callback payloads from calls, matching resource fields across units."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import jsonschema

ROOT = Path(__file__).resolve().parents[1]


class CallbackPayloadTest(unittest.TestCase):
    def test_payload_types_and_field_identity(self):
        header = r'''
typedef void *Widget;
typedef void *XtPointer;
typedef void *XtCallbackList;
typedef struct { int reason; } Payload;
typedef struct { XtCallbackList callback, opaque, ambiguous, unused; } Part;
typedef struct { Part part; } Instance;
typedef struct { XtCallbackList callback; } Other;
void XtCallCallbackList(Widget, XtCallbackList, XtPointer);
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
'''
        resources = r'''
#include "shared.h"
#define R(name, type, field) {name, "Callback", type, sizeof(XtCallbackList), __builtin_offsetof(Instance, part.field)}
Resource resources[] = {
    R("activateCallback", "Callback", callback),
    R("ordinary", "Pointer", callback),
    R("opaqueCallback", "Callback", opaque),
    R("ambiguousCallback", "Callback", ambiguous),
    R("unusedCallback", "Callback", unused),
    {"noOffsetCallback", "Callback", "Callback"}
};
struct { CoreClass core; } testRec = {{0, "Test", resources}};
WidgetClass testWidgetClass = &testRec.core;
'''
        calls = r'''
#include "shared.h"
void invoke(Instance *w, Other *other, Payload *pointer, XtPointer opaque) {
    Payload value;
    int different;
    XtCallCallbackList(w, w->part.callback, &value);
    XtCallCallbackList(w, w->part.callback, (XtPointer)pointer);
    XtCallCallbackList(w, w->part.callback, 0);
    XtCallCallbackList(w, other->callback, &different);
    XtCallCallbackList(w, w->part.opaque, opaque);
    XtCallCallbackList(w, w->part.ambiguous, &value);
    XtCallCallbackList(w, w->part.ambiguous, &different);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'shared.h').write_text(header)
            (path / 'resources.c').write_text(resources)
            (path / 'calls.c').write_text(calls)
            result = subprocess.run([sys.executable, str(ROOT / 'src/mkmir.py'),
                            str(path), '-o', str(path / 'mir.json')],
                           check=True, capture_output=True, text=True)
            artifact = json.loads((path / 'mir.json').read_text())
        entries = artifact['widgets']['testWidgetClass']['resources']
        self.assertEqual(entries['activateCallback']['callback_payload'], 'Payload')
        for name in ('ordinary', 'opaqueCallback', 'ambiguousCallback', 'unusedCallback',
                     'noOffsetCallback'):
            self.assertNotIn('callback_payload', entries[name])
        for name in ('opaqueCallback', 'ambiguousCallback', 'unusedCallback',
                     'noOffsetCallback'):
            self.assertIn(f'warning: testWidgetClass.{name}: could not determine '
                          'callback payload type', result.stderr)
        self.assertEqual(result.stderr.count('could not determine callback payload type'), 4)
        jsonschema.validate(artifact, json.loads(
            (ROOT / 'schema/mir.schema.json').read_text()))


if __name__ == '__main__':
    unittest.main()
