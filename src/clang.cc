/* mir -- wrapper around the clang-c API. */

#include "clang.h"

#include <vector>

namespace clang
{

    std::string toString(CXString s)
    {
        const char* c = clang_getCString(s);
        std::string res = (c != nullptr) ? c : "";
        clang_disposeString(s);
        return res;
    }

    /* Visitor trampoline for Cursor::visitChildren. */

    namespace
    {

        struct VisitorData
        {
            const Cursor::Visitor* _visitor = nullptr;
        };

        CXChildVisitResult visitorTrampoline(
            CXCursor cursor, CXCursor parent, CXClientData clientData)
        {
            auto* data = static_cast<VisitorData*>(clientData);
            Cursor cur(cursor);
            Cursor par(parent);
            return (*data->_visitor)(cur, par);
        }

    } /* namespace */

    unsigned Cursor::visitChildren(Visitor visitor) const
    {
        VisitorData data{&visitor};
        return clang_visitChildren(_cursor, visitorTrampoline, &data);
    }

    bool TranslationUnit::hasError() const
    {
        if (_tu == nullptr)
        {
            return true;
        }
        unsigned n = clang_getNumDiagnostics(_tu);
        for (unsigned i = 0; i < n; ++i)
        {
            CXDiagnostic d = clang_getDiagnostic(_tu, i);
            CXDiagnosticSeverity sev = clang_getDiagnosticSeverity(d);
            clang_disposeDiagnostic(d);
            if (sev == CXDiagnostic_Error || sev == CXDiagnostic_Fatal)
            {
                return true;
            }
        }
        return false;
    }

    TranslationUnit parse(Index& index,
        const std::string& fileName,
        const std::vector<std::string>& args,
        const std::string& unsavedFileName,
        const std::string& unsavedContents,
        unsigned options,
        CXErrorCode* error)
    {
        std::vector<const char*> cArgs;
        cArgs.reserve(args.size());
        for (auto& a : args)
        {
            cArgs.push_back(a.c_str());
        }

        CXUnsavedFile unsaved;
        unsaved.Filename = unsavedFileName.c_str();
        unsaved.Contents = unsavedContents.c_str();
        unsaved.Length = static_cast<unsigned long>(unsavedContents.size());

        CXTranslationUnit tu = nullptr;
        CXErrorCode ec = clang_parseTranslationUnit2(index.get(),
            fileName.c_str(),
            cArgs.data(),
            static_cast<int>(cArgs.size()),
            &unsaved,
            1,
            options,
            &tu);

        if (error != nullptr)
        {
            *error = ec;
        }

        if (ec != CXError_Success || tu == nullptr)
        {
            if (tu != nullptr)
            {
                clang_disposeTranslationUnit(tu);
            }
            return TranslationUnit();
        }

        return TranslationUnit(tu);
    }

} /* namespace clang */
