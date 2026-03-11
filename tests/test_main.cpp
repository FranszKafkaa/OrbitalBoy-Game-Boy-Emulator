#include "test_framework.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage(const char* argv0) {
    std::cerr << "uso: " << argv0 << " [--suite <nome>]... [--list-suites]\n";
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> suites;
    bool listSuites = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--suite") {
            if (i + 1 >= argc) {
                std::cerr << "faltou nome da suite apos --suite\n";
                printUsage(argv[0]);
                return 2;
            }
            suites.push_back(argv[++i]);
            continue;
        }

        if (arg == "--list-suites") {
            listSuites = true;
            continue;
        }

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }

        std::cerr << "opcao invalida: " << arg << '\n';
        printUsage(argv[0]);
        return 2;
    }

    if (listSuites) {
        for (const auto& suite : tests::listSuites()) {
            std::cout << suite << '\n';
        }
        return 0;
    }

    return tests::runTests(suites);
}
