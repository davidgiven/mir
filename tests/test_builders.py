"""Exercise the C++ builder's Xt Arg conversion without opening a display."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BuilderArgumentsTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("c++"), "C++ compiler required")
    def test_resource_value_pairs_become_args(self):
        source = r'''
#include <tuple>
#include <utility>
#include <cassert>
#include <cstring>
#include "cmotif.h"

template <typename T>
concept Buildable = requires(T& builder) { builder.build(); };
static_assert(!Buildable<CXmRowColumnBuilderImpl<CXmRowColumnBuilder,
    CXmRowColumn>>);
static_assert(Buildable<CXmRowColumnBuilder<>>);
static_assert(Buildable<CXmPulldownMenuBuilder<>>);

static Cardinal observed_count;
static XtArgVal observed_width;
static XtArgVal observed_data;
static Widget created = reinterpret_cast<Widget>(0x1234);

extern "C" Widget XmCreatePulldownMenu(Widget parent, char *name,
                                     ArgList args, Cardinal count) {
    assert(parent == nullptr);
    assert(std::strcmp(name, "pulldown") == 0);
    observed_count = count;
    if (count == 2) {
        assert(std::strcmp(args[0].name, "width") == 0);
        assert(std::strcmp(args[1].name, "userData") == 0);
        observed_width = args[0].value;
        observed_data = args[1].value;
    }
    return created;
}

int main() {
    CXmRowColumnBuilder empty(nullptr, "menu");
    assert(empty.buildArgList().empty());
    CXmPulldownMenuBuilder pulldown(nullptr, "pulldown");
    assert(pulldown.buildArgList().empty());

    int data = 42;
    auto builder = empty.setWidth(120).setUserData(&data).setX(-3);
    auto args = builder.buildArgList();
    assert(args.size() == 3);
    assert(std::strcmp(args[0].name, "width") == 0);
    assert(args[0].value == 120);
    assert(std::strcmp(args[1].name, "userData") == 0);
    assert(args[1].value == reinterpret_cast<XtArgVal>(&data));
    assert(std::strcmp(args[2].name, "x") == 0);
    assert(args[2].value == static_cast<XtArgVal>(-3));
    assert(static_cast<Widget>(pulldown.build()) == created);
    assert(observed_count == 0);
    auto menu = pulldown.setWidth(120).setUserData(&data).build();
    assert(static_cast<Widget>(menu) == created);
    assert(observed_count == 2);
    assert(observed_width == 120);
    assert(observed_data == reinterpret_cast<XtArgVal>(&data));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "test.cc").write_text(source)
            subprocess.run(
                ["c++", "-std=c++20", f"-I{ROOT / 'include'}",
                 str(path / "test.cc"), "-o", str(path / "test"),
                 "-lXm", "-lXt", "-lX11"],
                check=True,
            )
            subprocess.run([str(path / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
