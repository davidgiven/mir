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
typedef struct { int reason; char *value; } ChildPayload;
typedef struct { XtCallbackList callback, opaque, ambiguous, unused; } Part;
typedef struct { Part part; } Instance;
typedef struct { Part part; int extra; } ChildInstance;
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
    unsigned long widget_size;
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
/* Deliberately unrelated size record: offsets determine instance identity. */
struct { CoreClass core; } testRec = {{0, "Test", resources, sizeof(Other)}};
WidgetClass testWidgetClass = &testRec.core;
Resource childResources[] = {
    {"extra", "Extra", "Int", sizeof(int), __builtin_offsetof(ChildInstance, extra)},
    /* Inherited offsets must not identify the child's concrete instance. */
    {"baseValue", "Value", "Pointer", sizeof(XtCallbackList),
     __builtin_offsetof(Instance, part.callback)}
};
struct { CoreClass core; } childRec = {
    {&testRec.core, "Child", childResources, sizeof(Instance)}
};
WidgetClass childWidgetClass = &childRec.core;
'''
        calls = r'''
#include "shared.h"
void invoke(Instance *w, Other *other, Payload *pointer, XtPointer opaque) {
    Payload value;
    int different;
    XtCallCallbackList(w, w->part.callback, &value);
    XtCallCallbackList(w, w->part.callback, (XtPointer)pointer);
    XtCallCallbackList(w, w->part.callback,
                      (XtCallCallbackList(w, w->part.callback, &value), pointer));
    XtCallCallbackList(w, w->part.callback, 0);
    XtCallCallbackList(w, other->callback, &different);
    XtCallCallbackList(w, w->part.opaque, opaque);
    XtCallCallbackList(w, w->part.ambiguous, &value);
    XtCallCallbackList(w, w->part.ambiguous, &different);
}
void invokeChild(ChildInstance *child) {
    ChildPayload value;
    XtCallCallbackList((Widget)child, child->part.callback, &value);
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
        child = artifact['widgets']['childWidgetClass']
        self.assertEqual(child['superclass'], 'testWidgetClass')
        self.assertEqual(child['resources']['activateCallback'],
                         {**entries['activateCallback'], 'callback_payload': 'ChildPayload'})
        self.assertNotIn('ambiguousCallback', child['resources'])
        for name in ('ordinary', 'opaqueCallback', 'ambiguousCallback', 'unusedCallback',
                     'noOffsetCallback'):
            self.assertNotIn('callback_payload', entries[name])
        for name in ('opaqueCallback', 'unusedCallback',
                     'noOffsetCallback'):
            self.assertIn(f'warning: testWidgetClass.{name}: could not determine '
                          'callback payload type', result.stderr)
        self.assertEqual(result.stderr.count('could not determine callback payload type'), 3)
        conflict = next(line for line in result.stderr.splitlines()
                        if 'warning: testWidgetClass.ambiguousCallback:' in line)
        self.assertIn('conflicting callback payload types:', conflict)
        for payload, expression in (('Payload', '&value'), ('int', '&different')):
            line_number = next(number for number, line in enumerate(calls.splitlines(), 1)
                               if f'w->part.ambiguous, {expression}' in line)
            self.assertIn(f"'{payload}' at {path / 'calls.c'}:{line_number}:", conflict)
        self.assertEqual(result.stderr.count('conflicting callback payload types:'), 1)
        jsonschema.validate(artifact, json.loads(
            (ROOT / 'schema/mir.schema.json').read_text()))


if __name__ == '__main__':
    unittest.main()
