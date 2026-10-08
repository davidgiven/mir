/* mir -- Motif introspection / reflection. */

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <clang/AST/Decl.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Tooling/Tooling.h>

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>

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

/* Visitor that collects VarDecl of type WidgetClass and extern
 * FunctionDecls grouped by defining file. */
struct Visitor : public clang::RecursiveASTVisitor<Visitor>
{
    clang::SourceManager* _sm = nullptr;
    std::vector<std::pair<std::string, std::string>>* _widgetClasses = nullptr;
    std::map<std::string, std::vector<std::string>>* _functionsByFile = nullptr;

    bool VisitVarDecl(clang::VarDecl* decl)
    {
        if (clang::isa<clang::ParmVarDecl>(decl))
        {
            return true;
        }
        if (!decl->hasGlobalStorage())
        {
            return true;
        }
        std::string typeStr = decl->getType().getAsString();
        if (typeStr != "WidgetClass")
        {
            return true;
        }
        std::string name = decl->getNameAsString();
        if (name.empty())
        {
            return true;
        }
        clang::SourceLocation loc = decl->getLocation();
        if (!_sm->isInMainFile(loc))
        {
            return true;
        }
        std::string file = _sm->getFilename(_sm->getFileLoc(loc)).str();
        if (file.empty())
        {
            return true;
        }
        _widgetClasses->emplace_back(name, file);
        return true;
    }

    bool VisitFunctionDecl(clang::FunctionDecl* decl)
    {
        if (!decl->hasExternalFormalLinkage())
        {
            return true;
        }
        clang::SourceLocation loc = decl->getLocation();
        if (!_sm->isInMainFile(loc))
        {
            return true;
        }
        std::string file = _sm->getFilename(_sm->getFileLoc(loc)).str();
        if (file.empty())
        {
            return true;
        }
        std::string name = decl->getNameAsString();
        if (name.empty())
        {
            return true;
        }
        (*_functionsByFile)[file].push_back(name);
        return true;
    }
};

/* Compute the human-readable widget name by stripping the toolkit
 * prefix from the front and "WidgetClass" from the end, when present. */
static std::string computeHumanName(
    const std::string& name, const std::string& prefix)
{
    std::string result = name;
    if (!prefix.empty() && result.rfind(prefix, 0) == 0)
    {
        result = result.substr(prefix.size());
    }
    const std::string suffix = "WidgetClass";
    if (result.size() >= suffix.size() &&
        result.compare(result.size() - suffix.size(), suffix.size(), suffix) ==
            0)
    {
        result = result.substr(0, result.size() - suffix.size());
    }
    return result;
}

/* Case-insensitive prefix check: does `s` start with `prefix` ignoring
 * ASCII case? */
static bool startsWithIgnoreCase(
    const std::string& s, const std::string& prefix)
{
    if (prefix.size() > s.size())
    {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i)
    {
        unsigned char a = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(s[i])));
        unsigned char b = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(prefix[i])));
        if (a != b)
        {
            return false;
        }
    }
    return true;
}

/* Filter `functions` to only those starting with `prefix + humanName`
 * (case-insensitive). `prefix` and `humanName` are concatenated as-is and
 * then compared ignoring case. For each match, strip the prefix and widget
 * name to produce the method name. Returns a sorted map from method name to
 * an empty JSON object. */
static std::map<std::string, nlohmann::json> filterMethods(
    const std::vector<std::string>& functions,
    const std::string& prefix,
    const std::string& humanName)
{
    std::string wanted = prefix + humanName;
    std::map<std::string, nlohmann::json> result;
    for (auto& fn : functions)
    {
        if (startsWithIgnoreCase(fn, wanted))
        {
            std::string methodName = fn.substr(wanted.size());
            if (methodName.empty())
            {
                continue;
            }
            result.emplace(methodName, nlohmann::json::object());
        }
    }
    return result;
}

int main(int argc, char* argv[])
{
    /* Initialise Xt/Motif -- creates an application context needed for
     * any subsequent Xt/Motif calls. Dynamic reflection will later use
     * this context to instantiate widget hierarchies. */
    XtAppContext appContext = XtCreateApplicationContext();
    (void)appContext;

    std::vector<std::string> headerDirs;
    std::string outputPath;
    std::string prefix;

    static struct option longOptions[] = {
        {"headers", required_argument, nullptr, 'H'},
        {"output",  required_argument, nullptr, 'o'},
        {"prefix",  required_argument, nullptr, 'p'},
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

            case 'p':
                prefix = optarg;
                break;

            case '?':
            default:
                return 1;
        }
    }

    std::vector<fs::path> headers = collectHeaders(headerDirs);

    std::vector<std::pair<std::string, std::string>> widgetClasses;
    std::map<std::string, std::vector<std::string>> functionsByFile;

    /* Parse each header independently and leave unresolved symbols
     * unresolved. Use the C++ Tooling API (buildASTFromCodeWithArgs)
     * rather than the C API. */
    std::vector<std::string> args;

    for (auto& h : headers)
    {
        std::ifstream in(h);
        if (!in)
        {
            std::cerr << "warning: failed to open " << h << "\n";
            continue;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string code = buffer.str();

        auto ast = clang::tooling::buildASTFromCodeWithArgs(
            code, args, h.string(), "/usr/bin/clang");
        if (!ast)
        {
            std::cerr << "warning: failed to parse " << h << "\n";
            continue;
        }

        Visitor visitor;
        visitor._sm = &ast->getSourceManager();
        visitor._widgetClasses = &widgetClasses;
        visitor._functionsByFile = &functionsByFile;
        visitor.TraverseDecl(ast->getASTContext().getTranslationUnitDecl());
    }

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
            std::string humanName = computeHumanName(kv.first, prefix);
            auto filtered = filterMethods(it->second, prefix, humanName);
            for (auto& fn : filtered)
            {
                std::cout << "  " << fn.first << "\n";
            }
        }
    }

    if (!outputPath.empty())
    {
        nlohmann::json json = nlohmann::json::object();
        json["prefix"] = prefix;
        nlohmann::json widgets = nlohmann::json::object();
        /* Map humanName -> (classSymbol, include) sorted by humanName for
         * stable output keyed by human name. */
        std::map<std::string, std::pair<std::string, std::string>> humanMap;
        for (auto& kv : dedup)
        {
            std::string humanName = computeHumanName(kv.first, prefix);
            if (humanMap.find(humanName) != humanMap.end())
            {
                std::cerr << "warning: duplicate humanName '" << humanName
                          << "' for classSymbol '" << kv.first
                          << "' (already mapped to '"
                          << humanMap[humanName].first << "') -- skipping\n";
                continue;
            }
            humanMap[humanName] = {kv.first, kv.second};
        }
        for (auto& kv : humanMap)
        {
            const std::string& humanName = kv.first;
            const std::string& classSymbol = kv.second.first;
            const std::string& include = kv.second.second;
            nlohmann::json w = nlohmann::json::object();
            w["include"] = include;
            w["classSymbol"] = classSymbol;
            auto it = functionsByFile.find(include);
            if (it != functionsByFile.end())
            {
                auto methods = filterMethods(it->second, prefix, humanName);
                nlohmann::json m = nlohmann::json::object();
                for (auto& p : methods)
                {
                    m[p.first] = p.second;
                }
                w["methods"] = std::move(m);
            }
            else
            {
                w["methods"] = nlohmann::json::object();
            }
            widgets[humanName] = std::move(w);
        }
        json["widgets"] = std::move(widgets);
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
