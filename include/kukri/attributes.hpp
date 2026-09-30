#pragma once
#include "kukri/language.hpp"
#include <string>
#include <string_view>
#include <vector>
namespace kukri {
inline const auto& extensions = supported_extensions();

// Builds the complete managed block. newline must be LF or CRLF.
std::string attribute_block(std::string_view newline = "\n");

// Returns text with the managed block installed or removed. Unrelated bytes are
// preserved except for a separating newline when appending to an unterminated
// final line. Throws on duplicate or unmatched delimiters; performs no file I/O.
std::string update_attributes(const std::string& text, bool enable);

// Checks for the exact current managed block, accepting LF or CRLF. This does not
// check effective Git attributes. Throws if the managed delimiters are malformed.
bool attributes_healthy(const std::string& text);
}  // namespace kukri
