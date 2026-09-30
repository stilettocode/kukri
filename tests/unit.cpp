#include "kukri/cleaner.hpp"
#include "kukri/attributes.hpp"
#include "kukri/process.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace kukri;
static int checks=0;
static void require(bool condition, const std::string& name) { ++checks; if (!condition) throw std::runtime_error(name); }
static void fixture(const std::string& input, const std::string& expected, std::size_t count, Language language=Language::Cpp) {
    auto r=clean_source(input,language);
    require(r.output==expected,"output mismatch for: "+input+"\nActual: "+r.output);
    require(r.comments.size()==count,"token count");
    require(clean_source(r.output,language).output==r.output,"idempotence");
    require(std::count(input.begin(),input.end(),'\n')==std::count(r.output.begin(),r.output.end(),'\n'),"newline count");
}
int main() {
 try {
    fixture("", "",0);
    fixture("int a;\n//k private\nint b;\n","int a;\n\nint b;\n",1);
    fixture("int a;\n/*k\nprivate\ntext\nk*/\nint b;\n","int a;\n\n\n\n\nint b;\n",1);
    fixture("a/*k secret k*/b", "a b",1);
    fixture("+/*kk*/+", "+ +",1);
    fixture("/*kk*/", "",1);
    fixture("//k", "",1);
    fixture("//keep", "",1);
    fixture("//k one\n//k two\n//k three", "\n\n",3);
    fixture("// normal\n//k private\n/* normal */\n/*k private k*/", "// normal\n\n/* normal */\n",2);
    fixture("a\r\n//k private\r\n/*k\r\nx\r\nk*/\r\nb", "a\r\n\r\n\r\n\r\n\r\nb",2);
    fixture("//k \xc3\xa9 \xf0\x9f\x98\x80\n", "\n",1);
    fixture("//k continued\\\nsecret\nint x;", "\n\nint x;",1);
    const std::vector<std::string> preserved={
        "// ordinary //k /*k\n", "/* ordinary //k /*k syntax */", "///k docs\n//K caps\n// k spaced\n",
        "\"//k literal\"", "\"/*k literal k*/\"", "\"https://example.test\"",
        R"test("hello \" //k still string")test", R"test("backslashes \\" // ordinary)test",
        R"test('\'' '\\' '"' '/')test", R"test(R"(//k raw)")test",
        R"test(u8R"TAG(/*k raw k*/
//k also raw)TAG")test", "int n=1'000;"
    };
    for (auto& s:preserved) fixture(s,s,0);
    fixture("/\\\n/k secret\nint x;", "\n\nint x;",1);
    bool threw=false; try { clean_source("a\n/*k unterminated"); } catch (const std::exception& e) { threw=std::string(e.what()).find("line 2")!=std::string::npos; }
    require(threw,"unterminated private block diagnostic");
    threw=false; try { clean_source("\"unterminated //k"); } catch (...) { threw=true; } require(threw,"reject malformed literal");
    auto tokens=clean_source("x\n//k one\n/*kk*/").comments;
    require(tokens[0].offset==2 && tokens[0].line==2 && !tokens[0].block && tokens[1].line==3 && tokens[1].block,"locations");
    auto original="*.txt text\r\n";
    auto enabled=update_attributes(original,true);
    require(attributes_healthy(enabled),"healthy CRLF block");
    require(update_attributes(enabled,true)==enabled,"enable idempotence");
    require(update_attributes(enabled,false)==original,"preserve prefix");
    auto suffix=enabled+"*.other -text\r\n";
    require(update_attributes(suffix,false)==std::string(original)+"*.other -text\r\n","preserve suffix");
    require(update_attributes("",false).empty(),"disable empty");
    require(update_attributes(update_attributes("",true),false).empty(),"leave empty file");
    require(update_attributes("no final newline",true).starts_with("no final newline\n"),"separate appended block");
    for (auto text : {std::string("# BEGIN kukri managed rules\n"),std::string("# END kukri managed rules\n"),attribute_block()+attribute_block()}) {
        threw=false; try { update_attributes(text,true); } catch (...) { threw=true; } require(threw,"reject broken managed block");
    }
    require(shell_quote("/a b/it's$here.exe")=="'/a b/it'\\''s$here.exe'","POSIX shell quoting");
    std::cout << checks << " checks passed\n"; return 0;
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
