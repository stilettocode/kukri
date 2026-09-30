#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace kukri {
enum class Language { C, Cpp, Java, Go, Python, JavaScript, TypeScript };
struct LanguageExtension { std::string_view extension; Language language; };

// Shared, case-sensitive registry. Extensions omit the leading dot; returned
// entries and their string views have process lifetime.
const std::vector<LanguageExtension>& language_extensions();

// Selects by filename suffix without opening path; returns nullopt if unsupported.
std::optional<Language> language_for_path(std::string_view path);

// Parses a CLI language name; throws std::runtime_error for an unknown name.
Language language_named(std::string_view name);

// Registry extensions in stable order, with process lifetime.
const std::vector<std::string>& supported_extensions();
}  // namespace kukri
