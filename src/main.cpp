#include "kukri/cleaner.hpp"
#include "kukri/git.hpp"
#include "kukri/version.hpp"
#include "kukri/process.hpp"
#include <iostream>
#include <iterator>
#include <stdexcept>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
static int cli(const std::vector<std::string>& args) {
    auto argc=args.size();
    try {
        std::string command=argc==1?"help":args[1];
        std::string executable;
        auto language=kukri::Language::Cpp;
        if (argc>2) {
            if (argc!=4) throw std::runtime_error("expected one option and value; run kukri help");
            if (command=="enable" && args[2]=="--executable") {
                executable=args[3];
                if (executable.empty()) throw std::runtime_error("executable path must not be empty");
            } else if (command=="clean" && args[2]=="--language") language=kukri::language_named(args[3]);
            else if (command=="clean" && args[2]=="--path") {
                auto selected=kukri::language_for_path(args[3]);
                if (!selected) throw std::runtime_error("unsupported source extension; remove stale kukri attributes or choose --language");
                language=*selected;
            } else throw std::runtime_error("unknown option; run kukri help");
        }
        if (command=="help" || command=="--help" || command=="-h") {
            std::cout << "kukri - local-only source comments for Git\nUsage: kukri <command>\n"
                "  clean    Sanitize stdin to stdout\n  enable   Configure local required clean filter\n"
                "  disable  Remove local integration\n  status   Show repository configuration\n"
                "  verify   Check source blobs in the index\n  doctor   Diagnose configuration and test filter\n"
                "  version  Show version\n  help     Show help\n\nPrivate syntax: //k ... or /*k ... k*/; Python: #k ...\nclean [--path FILE | --language NAME] (default: cpp; reads stdin to EOF)\nLanguages: c, cpp, java, go, python, javascript, typescript\nEnable option: --executable ABSOLUTE_PATH (preserve a stable installation link)\n";
            return 0;
        }
        if (command=="version" || command=="--version") { std::cout << kukri::version << '\n'; return 0; }
        if (command=="clean" || command=="smudge") {
#ifdef _WIN32
            if (_setmode(_fileno(stdin),_O_BINARY)==-1 || _setmode(_fileno(stdout),_O_BINARY)==-1) throw std::runtime_error("cannot set binary stdio");
#endif
            std::string source{std::istreambuf_iterator<char>(std::cin),{}};
            if (std::cin.bad()) throw std::runtime_error("failed to read stdin");
            // Finish scanning before writing: required filters must never receive
            // a partial successful-looking result after a lexical error.
            auto output=command=="clean" ? kukri::clean_source(source,language).output : std::move(source);
            std::cout.write(output.data(),static_cast<std::streamsize>(output.size())); std::cout.flush();
            if (!std::cout) throw std::runtime_error("failed to write stdout");
            return 0;
        }
        if (command=="enable" || command=="disable" || command=="status" || command=="verify" || command=="doctor") return kukri::repository_command(command,executable);
        throw std::runtime_error("unknown command '"+command+"'; run kukri help");
    } catch (const std::exception& e) { std::cerr << "kukri: " << e.what() << '\n'; return 1; }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i=0;i<argc;++i) args.push_back(kukri::path_text(std::filesystem::path(argv[i])));
    return cli(args);
}
#else
int main(int argc, char** argv) { return cli(std::vector<std::string>(argv,argv+argc)); }
#endif
