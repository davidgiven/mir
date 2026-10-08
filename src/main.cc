/* mir -- Motif introspection / reflection. */

#include <algorithm>
#include <clang-c/Index.h>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

/* Collect all header files under the given directories. */
static std::vector<fs::path> collectHeaders(
    const std::vector<std::string>& headerDirs)
{
    std::vector<fs::path> headers;

    for (const auto& dirStr : headerDirs)
    {
        fs::path dir(dirStr);
        if (!fs::exists(dir))
        {
            continue;
        }
        if (fs::is_regular_file(dir))
        {
            headers.push_back(dir);
            continue;
        }
        if (!fs::is_directory(dir))
        {
            continue;
        }
        for (auto& entry : fs::recursive_directory_iterator(dir))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            auto ext = entry.path().extension().string();
            if (ext == ".h" || ext == ".H")
            {
                headers.push_back(entry.path());
            }
        }
    }

    std::sort(headers.begin(), headers.end());

    return headers;
}

/* Data passed to the libclang visitor. */
struct VisitorData
{
    std::vector<std::string>* _out;
};

/* libclang visitor: collect VarDecl of type WidgetClass. */
static enum CXChildVisitResult visitor(
    CXCursor cursor, CXCursor parent, CXClientData clientData)
{
    (void)parent;

    if (clang_getCursorKind(cursor) == CXCursor_VarDecl)
    {
        CXType type = clang_getCursorType(cursor);
        CXString typeSpelling = clang_getTypeSpelling(type);
        std::string spelling = clang_getCString(typeSpelling);
        clang_disposeString(typeSpelling);

        if (spelling == "WidgetClass")
        {
            CXString name = clang_getCursorSpelling(cursor);
            auto* data = static_cast<VisitorData*>(clientData);
            data->_out->push_back(clang_getCString(name));
            clang_disposeString(name);
        }
    }

    return CXChildVisit_Recurse;
}

int main(int argc, char* argv[])
{
    std::vector<std::string> headerDirs;
    std::vector<std::string> includeDirs;

    static struct option longOptions[] = {
        {"headers", required_argument, nullptr, 'H'},
        {"include", required_argument, nullptr, 'I'},
        {nullptr,   0,                 nullptr, 0  },
    };

    int opt = 0;
    int longIndex = 0;
    while ((opt = getopt_long(argc, argv, "I:", longOptions, &longIndex)) != -1)
    {
        switch (opt)
        {
            case 'H':
                headerDirs.emplace_back(optarg);
                break;

            case 'I':
                includeDirs.emplace_back(optarg);
                break;

            case '?':
            default:
                return 1;
        }
    }

    std::vector<fs::path> headers = collectHeaders(headerDirs);

    fs::path combined = fs::temp_directory_path() / "mir_combined.c";
    {
        std::ofstream out(combined);
        if (!out)
        {
            std::cerr << "failed to create " << combined << "\n";
            return 1;
        }
        for (auto& h : headers)
        {
            out << "#include \"" << h.string() << "\"\n";
        }
    }

    /* Build clang arguments from explicit --include directories. */
    std::vector<std::string> argStrs;
    for (auto& d : includeDirs)
    {
        argStrs.push_back("-I" + d);
    }

    std::vector<const char*> args;
    args.reserve(argStrs.size());
    for (auto& s : argStrs)
    {
        args.push_back(s.c_str());
    }

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit tu = nullptr;
    CXErrorCode err = clang_parseTranslationUnit2(index,
        combined.c_str(),
        args.data(),
        static_cast<int>(args.size()),
        nullptr,
        0,
        CXTranslationUnit_None,
        &tu);

    if (err != CXError_Success || tu == nullptr)
    {
        std::cerr << "clang_parseTranslationUnit2 failed: " << err << "\n";
        clang_disposeIndex(index);
        return 1;
    }

    CXCursor root = clang_getTranslationUnitCursor(tu);
    std::vector<std::string> widgetClasses;
    VisitorData data{&widgetClasses};
    clang_visitChildren(root, visitor, &data);

    /* Deduplicate and sort for stable output. */
    std::set<std::string> sorted(widgetClasses.begin(), widgetClasses.end());

    for (auto& n : sorted)
    {
        std::cout << n << "\n";
    }

    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);

    std::error_code ec;
    fs::remove(combined, ec);

    return 0;
}
