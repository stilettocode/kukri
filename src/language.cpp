#include "kukri/language.hpp"
#include <stdexcept>
namespace kukri {
const std::vector<LanguageExtension>& language_extensions() {
    static const std::vector<LanguageExtension> table = {
        {"c",Language::C}, {"h",Language::Cpp}, {"cc",Language::Cpp},
        {"cpp",Language::Cpp}, {"cxx",Language::Cpp}, {"hh",Language::Cpp},
        {"hpp",Language::Cpp}, {"hxx",Language::Cpp}, {"java",Language::Java},
        {"go",Language::Go}, {"py",Language::Python}, {"pyi",Language::Python},
        {"js",Language::JavaScript}, {"mjs",Language::JavaScript}, {"cjs",Language::JavaScript},
        {"ts",Language::TypeScript}, {"mts",Language::TypeScript}, {"cts",Language::TypeScript}
    };
    return table;
}
std::optional<Language> language_for_path(std::string_view path) {
    auto dot=path.rfind('.');
    if (dot==path.npos) return {};
    for (const auto& item:language_extensions()) if (path.substr(dot+1)==item.extension) return item.language;
    return {};
}
Language language_named(std::string_view name) {
    if (name=="c") return Language::C;
    if (name=="cpp" || name=="c++") return Language::Cpp;
    if (name=="java") return Language::Java;
    if (name=="go") return Language::Go;
    if (name=="python") return Language::Python;
    if (name=="javascript") return Language::JavaScript;
    if (name=="typescript") return Language::TypeScript;
    throw std::runtime_error("unknown language; use c, cpp, java, go, python, javascript, or typescript");
}
const std::vector<std::string>& supported_extensions() {
    static const auto values=[] { std::vector<std::string> result;
        for (const auto& item:language_extensions()) result.emplace_back(item.extension);
        return result;
    }();
    return values;
}
}
