/* mir -- Motif introspection / reflection. */

#include <clang-c/Index.h>
#include <getopt.h>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    std::vector<std::string> headerDirs;

    static struct option longOptions[] = {
        {"headers", required_argument, nullptr, 'H'},
        {nullptr,   0,                 nullptr, 0  },
    };

    int opt = 0;
    int longIndex = 0;
    while ((opt = getopt_long(argc, argv, "", longOptions, &longIndex)) != -1)
    {
        switch (opt)
        {
            case 'H':
                headerDirs.emplace_back(optarg);
                break;

            case '?':
            default:
                return 1;
        }
    }

    (void)headerDirs;

    CXIndex index = clang_createIndex(0, 0);
    clang_disposeIndex(index);
    return 0;
}
