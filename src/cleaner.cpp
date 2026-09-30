#include "kukri/cleaner.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
namespace kukri {
namespace {
bool ident(char c) { auto b=static_cast<unsigned char>(c); return std::isalnum(b) || c=='_' || c=='$' || b>=128; }
int hex(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
// Half-open spans in the translated lexical view, not original byte offsets.
struct Removal { std::size_t start,end; bool block; };
// Lex translated characters, but always render from original bytes. This permits
// Java Unicode escapes and C translation-phase splices without normalizing files.
class Scanner {
    std::string_view original;
    Language language;
    std::string text;
    // Each lexical byte maps to an original byte span and source line.
    std::vector<std::size_t> offsets, ends, lines;
    std::vector<Removal> removals;
    std::size_t i=0;
    bool cpp() const { return language==Language::C || language==Language::Cpp; }
    bool js() const { return language==Language::JavaScript || language==Language::TypeScript; }
    bool at(std::string_view t) const { return text.compare(i,t.size(),t)==0; }
    [[noreturn]] void fail(const std::string& message, std::size_t pos) const {
        auto line=pos<lines.size()?lines[pos]:(lines.empty()?1:lines.back());
        throw std::runtime_error(message+" at line "+std::to_string(line)+"; refusing to emit filtered source");
    }
    void depth_check(unsigned depth) const { if (depth>128) fail("literal/interpolation nesting exceeds 128",i); }
    bool newline(std::size_t p) const {
        return p<text.size() && (text[p]=='\r' || text[p]=='\n' ||
            (js() && (text.compare(p,3,"\xe2\x80\xa8")==0 || text.compare(p,3,"\xe2\x80\xa9")==0)));
    }
    void prepare() {
        std::size_t line=1, slashes=0; bool translated=false;
        for (std::size_t p=0;p<original.size();) {
            auto start=p; char c=original[p++];
            if (cpp() && c=='?' && original.substr(start,3)=="?" "?/")
                throw std::runtime_error("C/C++ trigraph backslash is unsupported; refusing to emit filtered source");
            if (cpp() && c=='\\' && p<original.size() && (original[p]=='\n' || original[p]=='\r')) {
                if (original[p++]=='\r' && p<original.size() && original[p]=='\n') ++p;
                ++line; continue;
            }
            // Java escape eligibility depends on prior translated backslashes,
            // not simply on whether the raw input contains a backslash-u pair.
            bool escaped=false;
            if (language==Language::Java && c=='\\' && (translated || slashes%2==0) && p<original.size() && original[p]=='u') {
                while (p<original.size() && original[p]=='u') ++p;
                unsigned value=0;
                for (unsigned n=0;n<4;++n) {
                    if (p==original.size() || hex(original[p])<0)
                        throw std::runtime_error("malformed Java Unicode escape at line "+std::to_string(line));
                    value=value*16+static_cast<unsigned>(hex(original[p++]));
                }
                // Non-ASCII UTF-16 units cannot be ASCII delimiters; an opaque byte
                // preserves their role without changing original output bytes.
                c=value<128?static_cast<char>(value):static_cast<char>(0x80); escaped=true;
            }
            offsets.push_back(start); ends.push_back(p); lines.push_back(line); text+=c;
            if (original[start]=='\n') ++line;
            slashes=c=='\\'?slashes+1:0; translated=escaped;
        }
    }
    void comment(bool block, std::size_t prefix) {
        auto start=i; bool priv=text[i+prefix]=='k';
        i+=prefix+(priv?1:0);
        if (block) {
            auto end=text.find(priv?"k*/":"*/",i);
            if (end==text.npos) fail(priv?"unterminated private block comment":"unterminated block comment",start);
            i=end+(priv?3:2);
        } else { while (i<text.size() && !newline(i)) ++i; }
        if (priv) removals.push_back({start,i,block});
    }
    void skip_escape() {
        if (i+2<text.size() && text[i+1]=='\r' && text[i+2]=='\n') i+=3;
        else i+=std::min<std::size_t>(2,text.size()-i);
    }
    void quoted(char quote, bool triple=false) {
        auto start=i; auto delimiter=std::string(triple?3:1,quote); i+=delimiter.size();
        while (i<text.size()) {
            if (at(delimiter)) { i+=delimiter.size(); return; }
            if (text[i]=='\\') { skip_escape(); continue; }
            if (!triple && newline(i)) fail("unterminated quoted literal",start);
            ++i;
        }
        fail("unterminated quoted literal",start);
    }
    void raw_cpp(std::size_t prefix) {
        auto start=i; auto delim_start=i+prefix;
        auto open=text.find('(',delim_start);
        if (open==text.npos || open-delim_start>16 || text.substr(delim_start,open-delim_start).find_first_of(" \\)\t\r\n")!=text.npos)
            fail("invalid C++ raw string delimiter",start);
        auto end=text.find(")"+text.substr(delim_start,open-delim_start)+"\"",open+1);
        if (end==text.npos) fail("unterminated C++ raw string",start);
        i=end+open-delim_start+2;
        // Raw strings undo phase-2 splicing; reject that rare case instead of
        // accidentally recognizing a delimiter manufactured by normalization.
        auto raw=original.substr(offsets[start],ends[i-1]-offsets[start]);
        if (raw.find("\\\n")!=raw.npos || raw.find("\\\r")!=raw.npos)
            fail("line splice inside C++ raw literal is unsupported",start);
    }
    void regex() {
        auto start=i++; bool bracket=false;
        while (i<text.size() && !newline(i)) {
            if (text[i]=='\\') { if (i+1==text.size() || newline(i+1)) break; i+=2; continue; }
            if (text[i]=='[') {
                if (bracket) fail("nested regex character classes are unsupported",start);
                bracket=true;
            } else if (text[i]==']') bracket=false;
            else if (text[i]=='/' && !bracket) {
                ++i; while (i<text.size() && ident(text[i])) ++i; return;
            }
            ++i;
        }
        fail("unterminated regular expression",start);
    }
    void js_template(unsigned depth) {
        depth_check(depth); auto start=i++;
        while (i<text.size()) {
            if (text[i]=='\\') { skip_escape(); continue; }
            if (text[i]=='`') { ++i; return; }
            if (at("${")) { i+=2; code(true,depth+1); continue; }
            ++i;
        }
        fail("unterminated template literal",start);
    }
    void python_string(bool formatted, bool raw, unsigned depth) {
        depth_check(depth); auto start=i; char q=text[i];
        auto delimiter=std::string(at(std::string(3,q))?3:1,q); i+=delimiter.size();
        while (i<text.size()) {
            if (at(delimiter)) { i+=delimiter.size(); return; }
            if (text[i]=='\\') {
                // In f-strings backslashes do not escape replacement-field braces.
                if (formatted && i+1<text.size() && (text[i+1]=='{' || text[i+1]=='}')) { ++i; continue; }
                // Named Unicode escapes contain braces, but are literal string content.
                if (formatted && !raw && at("\\N{")) {
                    auto end=text.find('}',i+3); if (end==text.npos) fail("unterminated named Unicode escape",i);
                    i=end+1; continue;
                }
                skip_escape(); continue;
            }
            if (formatted && (at("{{") || at("}}"))) { i+=2; continue; }
            if (formatted && text[i]=='{') { ++i; python_code(true,depth+1); continue; }
            if (formatted && text[i]=='}') fail("unmatched f-string closing brace",i);
            if (delimiter.size()==1 && newline(i)) fail("unterminated Python string",start);
            ++i;
        }
        fail("unterminated Python string",start);
    }
    void python_format(unsigned depth) {
        depth_check(depth);
        while (i<text.size()) {
            if (text[i]=='}') { ++i; return; }
            if (text[i]=='{') { ++i; python_code(true,depth+1); }
            else ++i;
        }
        fail("unterminated f-string format specification",i);
    }
    void python_code(bool field, unsigned depth) {
        depth_check(depth); std::vector<char> brackets;
        while (i<text.size()) {
            char c=text[i];
            if (c=='#') { comment(false,1); continue; }
            if (c=='\'' || c=='"') { python_string(false,false,depth+1); continue; }
            if (std::isalpha(static_cast<unsigned char>(c)) || c=='_') {
                auto start=i++; while (i<text.size() && ident(text[i])) ++i;
                auto word=text.substr(start,i-start);
                std::transform(word.begin(),word.end(),word.begin(),[](unsigned char b){ return static_cast<char>(std::tolower(b)); });
                if (i<text.size() && (text[i]=='\'' || text[i]=='"')) {
                    if (word=="t" || word=="tr" || word=="rt") fail("Python template strings are not supported",start);
                    if (word=="f" || word=="fr" || word=="rf" || word=="r" || word=="b" || word=="br" || word=="rb" || word=="u") {
                        python_string(word.find('f')!=word.npos,word.find('r')!=word.npos,depth+1);
                    }
                }
                continue;
            }
            if (field && brackets.empty()) {
                if (c=='}') { ++i; return; }
                if (c==':') { ++i; python_format(depth+1); return; }
                if (c=='!' && !at("!=")) {
                    ++i;
                    if (i==text.size() || std::string_view("sra").find(text[i])==std::string_view::npos) fail("invalid f-string conversion",i);
                    ++i; continue;
                }
            }
            if (c=='(' || c=='[' || c=='{') brackets.push_back(c);
            else if (c==')' || c==']' || c=='}') {
                if (brackets.empty() || brackets.back()!=(c==')'?'(':c==']'?'[':'{')) fail("unbalanced Python delimiter",i);
                brackets.pop_back();
            }
            ++i;
        }
        if (field || !brackets.empty()) fail("unterminated Python expression",i);
    }
    void code(bool interpolation, unsigned depth) {
        depth_check(depth);
        enum class Slash { Regex, Division, Ambiguous };
        Slash slash=Slash::Regex;
        bool control=false, property=false, linebreak=false;
        std::vector<bool> parens;
        std::size_t braces=0;
        while (i<text.size()) {
            char c=text[i];
            if (js() && newline(i) && c!='\r' && c!='\n') { i+=3; linebreak=true; continue; }
            if (std::isspace(static_cast<unsigned char>(c))) { if (newline(i)) linebreak=true; ++i; continue; }
            if (at("//")) { comment(false,2); continue; }
            if (at("/*")) {
                auto start=i; comment(true,2);
                if (text.substr(start,i-start).find_first_of("\r\n")!=text.npos) linebreak=true;
                continue;
            }
            bool token_after_newline=linebreak; linebreak=false;
            if (i==0 && at("\xef\xbb\xbf")) { i+=3; continue; }
            if (js() && (i==0 || (i==3 && text.compare(0,3,"\xef\xbb\xbf")==0)) && at("#!")) { while(i<text.size() && !newline(i)) ++i; continue; }
            if (js() && (at("<!--") || at("-->"))) fail("legacy JavaScript HTML comments are unsupported",i);
            if (js() && c=='\\') fail("escaped JavaScript identifiers are unsupported",i);
            if (cpp() && c=='#') {
                auto p=i+1; while (p<text.size() && (text[p]==' ' || text[p]=='\t')) ++p;
                auto begin=p; while (p<text.size() && ident(text[p])) ++p;
                auto directive=text.substr(begin,p-begin);
                if (directive=="include" || directive=="include_next" || directive=="import") {
                    while (p<text.size() && (text[p]==' ' || text[p]=='\t')) ++p;
                    if (p<text.size() && text[p]=='<') {
                        auto end=text.find('>',p+1);
                        if (end==text.npos || text.substr(p,end-p).find_first_of("\r\n")!=text.npos) fail("unterminated include header",i);
                        i=end+1; continue;
                    }
                }
            }
            if (language==Language::Cpp) {
                bool found=false;
                for (auto prefix:{"R\"","u8R\"","uR\"","UR\"","LR\""}) {
                    if (at(prefix)) { raw_cpp(std::string_view(prefix).size()); found=true; break; }
                }
                if (found) continue;
            }
            if (c=='`' && language==Language::Go) {
                auto end=text.find('`',i+1); if (end==text.npos) fail("unterminated Go raw string",i);
                // A backslash cannot escape a Go raw-string terminator.
                i=end+1; continue;
            }
            if (c=='`' && js()) { js_template(depth+1); slash=Slash::Division; continue; }
            if (c=='\'' || c=='"') {
                quoted(c,language==Language::Java && at("\"\"\"")); slash=Slash::Division; continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                ++i;
                while (i<text.size() && (ident(text[i]) || text[i]=='.' || (cpp() && text[i]=='\''))) ++i;
                slash=Slash::Division; continue;
            }
            if (ident(c)) {
                auto start=i++; while (i<text.size() && ident(text[i])) ++i;
                auto word=text.substr(start,i-start);
                if (js() && control && word=="await") fail("for-await lexical context is unsupported",start);
                control=!property && (word=="if" || word=="while" || word=="for" || word=="with" || word=="switch" || word=="catch");
                if (js()) {
                    slash=(control || word=="return" || word=="throw" || word=="case" || word=="yield" || word=="await" || word=="delete" || word=="void" || word=="typeof" || word=="new" || word=="in" || word=="instanceof" || word=="else" || word=="do")?Slash::Regex:Slash::Division;
                    if (word=="of" || word=="yield" || word=="await" || word=="break" || word=="continue") slash=Slash::Ambiguous;
                    if (property) slash=Slash::Division;
                    property=false;
                }
                continue;
            }
            if (c=='/' && js()) {
                if (slash==Slash::Ambiguous || (slash==Slash::Division && token_after_newline)) fail("ambiguous JavaScript/TypeScript slash; parenthesize the expression or regex",i);
                if (slash==Slash::Regex) { regex(); slash=Slash::Division; }
                else { ++i; if (i<text.size() && text[i]=='=') ++i; slash=Slash::Regex; } continue;
            }
            if (c=='(') { parens.push_back(control); control=false; slash=Slash::Regex; }
            else if (c==')') {
                slash=!parens.empty() && parens.back()?Slash::Regex:Slash::Division;
                if (!parens.empty()) parens.pop_back();
            } else if (c=='{') { ++braces; slash=Slash::Regex; }
            else if (c=='}') {
                if (interpolation && !braces) { ++i; return; }
                if (braces) --braces;
                // Distinguishing object/function expressions from statement blocks
                // requires a parser. Refuse an immediately following slash instead.
                slash=Slash::Ambiguous;
            } else if (c==']') slash=Slash::Division;
            else if ((c=='+' || c=='-') && i+1<text.size() && text[i+1]==c) { ++i; slash=Slash::Ambiguous; }
            else if (c=='.') { slash=Slash::Division; property=true; }
            else if (language==Language::TypeScript && (c=='!' || c=='>')) slash=Slash::Ambiguous;
            else slash=Slash::Regex;
            ++i;
        }
        if (interpolation) fail("unterminated template interpolation",i);
    }
public:
    Scanner(std::string_view s, Language l):original(s),language(l) { prepare(); }
    CleanResult run() {
        if (language==Language::Python) python_code(false,0); else code(false,0);
        CleanResult result; result.output.reserve(original.size()); std::size_t cursor=0;
        // Recursive scanning appends disjoint spans in source order. Render from
        // original bytes so translation never normalizes unrelated source.
        for (const auto& range:removals) {
            auto begin=offsets[range.start];
            // A line comment owns any spliced bytes up to its logical line end;
            // a block stops at its terminator and must not consume a later splice.
            auto end=range.block?ends[range.end-1]:(range.end<offsets.size()?offsets[range.end]:original.size());
            result.output.append(original.substr(cursor,begin-cursor));
            result.comments.push_back({begin,lines[range.start],range.block});
            if (range.block && !result.output.empty() && !std::isspace(static_cast<unsigned char>(result.output.back()))) result.output+=' ';
            for (auto p=begin;p<end;++p) {
                if (original[p]=='\n' || original[p]=='\r') result.output+=original[p];
                else if (js() && (original.substr(p,3)=="\xe2\x80\xa8" || original.substr(p,3)=="\xe2\x80\xa9")) {
                    result.output.append(original.substr(p,3)); p+=2;
                }
            }
            cursor=end;
        }
        result.output.append(original.substr(cursor)); return result;
    }
};
}
CleanResult clean_source(std::string_view source, Language language) { return Scanner(source,language).run(); }
}
