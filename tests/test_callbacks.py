"""Exercise typed and erased callback registration without opening a display."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CallbackRegistrationTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
    def test_typed_callbacks_and_existing_registration(self):
        source = r'''
#include "cmotif.h"
#include <cassert>
#include <string>
#include <vector>

struct Registration { XtCallbackProc function; XtPointer closure; };
static std::map<std::string, std::vector<Registration>> registrations;
static XtEventHandler event_handler;
static XtPointer event_closure;

extern "C" void XtAddCallback(Widget, const char* name,
                               XtCallbackProc function, XtPointer closure) {
    registrations[name].push_back({function, closure});
}
extern "C" void XtAddEventHandler(Widget, EventMask mask, _XtBoolean,
                                   XtEventHandler handler, XtPointer closure) {
    assert(mask == ExposureMask);
    event_handler = handler;
    event_closure = closure;
}

static void invoke(Widget widget, const char* name, XtPointer payload) {
    for (auto registration : registrations.at(name))
        registration.function(widget, registration.closure, payload);
}

int main() {
    static_assert(std::is_same_v<CWidgetImplBase::Callback<XmPushButtonCallbackStruct>,
                                std::function<void(XmPushButtonCallbackStruct*)>>);
    Widget raw = reinterpret_cast<Widget>(0x1234);
    CXmPushButton widget(raw);
    XmPushButtonCallbackStruct payload{};
    payload.click_count = 3;
    int typed_calls = 0, erased_calls = 0, event_calls = 0;
    std::weak_ptr<int> lifetime;
    {
        auto token = std::make_shared<int>(42);
        lifetime = token;
        auto& result = widget.addCallback<XmPushButtonCallbackStruct>("typed",
            [&, token](XmPushButtonCallbackStruct* data) {
                assert(data == &payload);
                assert(data->click_count == 3);
                ++typed_calls;
            });
        assert(&result == &widget);
    }
    assert(!lifetime.expired());
    CWidgetImplBase::Callback<XmAnyCallbackStruct> second =
        [](XmAnyCallbackStruct* data) { assert(data == nullptr); };
    widget.addCallback("second", std::move(second));
    widget.addCallback<void>("explicitVoid", [](void* data) { assert(!data); });
    // Existing generated accessor still accepts an XtPointer callback.
    widget.addActivateCallback([&](XtPointer data) {
        assert(data == &payload);
        ++erased_calls;
    });
    widget.addEventHandler([&](XEvent* event, Boolean* dispatch) {
        assert(event->type == Expose);
        *dispatch = false;
        ++event_calls;
    }, ExposureMask);
    invoke(raw, "typed", &payload);
    invoke(raw, "second", nullptr);
    invoke(raw, "explicitVoid", nullptr);
    invoke(raw, "activateCallback", &payload);
    XEvent event{};
    event.type = Expose;
    Boolean dispatch = true;
    event_handler(raw, event_closure, &event, &dispatch);
    assert(!dispatch);
    assert(typed_calls == 1 && erased_calls == 1 && event_calls == 1);
    assert(registrations.at("destroyCallback").size() == 1);
    invoke(raw, "destroyCallback", nullptr);
    assert(lifetime.expired());
    invoke(raw, "typed", &payload);
    invoke(raw, "activateCallback", &payload);
    event_handler(raw, event_closure, &event, &dispatch);
    assert(typed_calls == 1 && erased_calls == 1 && event_calls == 1);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cc').write_text(source)
            subprocess.run(
                ['c++', '-std=c++20', f'-I{ROOT / "include"}', f'-I{ROOT}',
                 str(path / 'test.cc'), str(ROOT / 'src/cmotif.cc'),
                 '-o', str(path / 'test'), '-lXm', '-lXt', '-lX11'], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
