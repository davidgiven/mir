/* mir -- Motif introspection / reflection. */

#include <algorithm>
#include <clang-c/Index.h>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

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
    std::vector<std::pair<std::string, std::string>>* _widgetClasses;
    std::map<std::string, std::vector<std::string>>* _functionsByFile;
};

/* libclang visitor: collect VarDecl of type WidgetClass and extern
 * FunctionDecls grouped by defining file. */
static enum CXChildVisitResult visitor(
    CXCursor cursor, CXCursor parent, CXClientData clientData)
{
    (void)parent;

    auto* data = static_cast<VisitorData*>(clientData);
    CXCursorKind kind = clang_getCursorKind(cursor);

    if (kind == CXCursor_VarDecl)
    {
        CXType type = clang_getCursorType(cursor);
        CXString typeSpelling = clang_getTypeSpelling(type);
        std::string spelling = clang_getCString(typeSpelling);
        clang_disposeString(typeSpelling);

        if (spelling == "WidgetClass")
        {
            CXString name = clang_getCursorSpelling(cursor);
            std::string nameStr = clang_getCString(name);
            clang_disposeString(name);

            CXSourceLocation loc = clang_getCursorLocation(cursor);
            CXFile file = nullptr;
            unsigned line = 0;
            unsigned column = 0;
            unsigned offset = 0;
            clang_getFileLocation(loc, &file, &line, &column, &offset);
            std::string fileStr;
            if (file != nullptr)
            {
                CXString fileName = clang_getFileName(file);
                const char* cstr = clang_getCString(fileName);
                if (cstr != nullptr)
                {
                    fileStr = cstr;
                }
                clang_disposeString(fileName);
            }

            data->_widgetClasses->emplace_back(nameStr, fileStr);
        }
    }
    else if (kind == CXCursor_FunctionDecl)
    {
        /* Only extern linkage functions. */
        CXLinkageKind linkage = clang_getCursorLinkage(cursor);
        if (linkage != CXLinkage_External)
        {
            return CXChildVisit_Recurse;
        }

        CXSourceLocation loc = clang_getCursorLocation(cursor);
        CXFile file = nullptr;
        unsigned line = 0;
        unsigned column = 0;
        unsigned offset = 0;
        clang_getFileLocation(loc, &file, &line, &column, &offset);
        if (file == nullptr)
        {
            return CXChildVisit_Recurse;
        }

        CXString fileName = clang_getFileName(file);
        const char* cstr = clang_getCString(fileName);
        std::string fileStr;
        if (cstr != nullptr)
        {
            fileStr = cstr;
        }
        clang_disposeString(fileName);

        if (fileStr.empty())
        {
            return CXChildVisit_Recurse;
        }

        CXString name = clang_getCursorSpelling(cursor);
        const char* nstr = clang_getCString(name);
        std::string nameStr;
        if (nstr != nullptr)
        {
            nameStr = nstr;
        }
        clang_disposeString(name);

        if (nameStr.empty())
        {
            return CXChildVisit_Recurse;
        }

        (*data->_functionsByFile)[fileStr].push_back(nameStr);
    }

    return CXChildVisit_Recurse;
}

int main(int argc, char* argv[])
{
    std::vector<std::string> headerDirs;
    std::string outputPath;

    static struct option longOptions[] = {
        {"headers", required_argument, nullptr, 'H'},
        {"output",  required_argument, nullptr, 'o'},
        {nullptr,   0,                 nullptr, 0  },
    };

    int opt = 0;
    int longIndex = 0;
    while ((opt = getopt_long(argc, argv, "o:", longOptions, &longIndex)) != -1)
    {
        switch (opt)
        {
            case 'H':
                headerDirs.emplace_back(optarg);
                break;

            case 'o':
                outputPath = optarg;
                break;

            case '?':
            default:
                return 1;
        }
    }

    std::vector<fs::path> headers = collectHeaders(headerDirs);

    CXIndex index = clang_createIndex(0, 0);
    std::vector<std::pair<std::string, std::string>> widgetClasses;
    std::map<std::string, std::vector<std::string>> functionsByFile;
    VisitorData data{&widgetClasses, &functionsByFile};

    for (auto& h : headers)
    {
        CXTranslationUnit tu = nullptr;
        CXErrorCode err = clang_parseTranslationUnit2(index,
            h.c_str(),
            nullptr,
            0,
            nullptr,
            0,
            CXTranslationUnit_Incomplete | CXTranslationUnit_KeepGoing,
            &tu);

        if (err != CXError_Success || tu == nullptr)
        {
            std::cerr << "warning: failed to parse " << h << ": " << err
                      << "\n";
            if (tu != nullptr)
            {
                clang_disposeTranslationUnit(tu);
            }
            continue;
        }

        CXCursor root = clang_getTranslationUnitCursor(tu);
        clang_visitChildren(root, visitor, &data);
        clang_disposeTranslationUnit(tu);
    }

    clang_disposeIndex(index);

    /* Deduplicate widget classes by name and sort for stable output. */
    std::map<std::string, std::string> dedup;
    for (auto& p : widgetClasses)
    {
        if (dedup.find(p.first) == dedup.end())
        {
            dedup[p.first] = p.second;
        }
    }

    /* Deduplicate and sort functions per file. */
    for (auto& kv : functionsByFile)
    {
        auto& vec = kv.second;
        std::sort(vec.begin(), vec.end());
        vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
    }

    for (auto& kv : dedup)
    {
        std::cout << kv.first << " " << kv.second << "\n";
        auto it = functionsByFile.find(kv.second);
        if (it != functionsByFile.end())
        {
            for (auto& fn : it->second)
            {
                std::cout << "  " << fn << "\n";
            }
        }
    }

    if (!outputPath.empty())
    {
        nlohmann::json json = nlohmann::json::object();
        std::ofstream out(outputPath);
        if (!out)
        {
            std::cerr << "failed to open " << outputPath << "\n";
            return 1;
        }
        out << json.dump(2) << "\n";
        if (!out)
        {
            std::cerr << "failed to write " << outputPath << "\n";
            return 1;
        }
    }

    return 0;
}
