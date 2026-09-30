#pragma once
#include "kukri/language.hpp"
#include <string>
#include <string_view>
#include <vector>
namespace kukri {
// Locations refer to the original input, before lexical translation or removal.
struct Occurrence {
    std::size_t offset;  // Zero-based byte offset of the opening marker.
    std::size_t line;    // One-based source line; LF/CRLF input is supported.
    bool block;
};
struct CleanResult {
    std::string output;
    std::vector<Occurrence> comments;  // In source order.
};

// Removes private comments recognized by language without modifying source.
// Preserves newline bytes and inserts separation where block removal could join
// tokens. Throws std::runtime_error for malformed or explicitly unsupported syntax;
// no partial result is returned. The returned data owns its storage.
CleanResult clean_source(std::string_view source, Language language = Language::Cpp);
}  // namespace kukri
