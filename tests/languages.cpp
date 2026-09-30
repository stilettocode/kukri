#include "kukri/cleaner.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <random>
using namespace kukri;
int checks=0;
void fixture(Language l, const std::string& in, const std::string& out, std::size_t count) {
    ++checks; auto r=clean_source(in,l);
    if (r.output!=out || r.comments.size()!=count || clean_source(out,l).output!=out ||
        std::count(in.begin(),in.end(),'\n')!=std::count(out.begin(),out.end(),'\n'))
        throw std::runtime_error("fixture failed: "+in+"\nActual: "+r.output);
}
void unchanged(Language l, const std::string& s) { fixture(l,s,s,0); }
void rejects(Language l, const std::string& s) {
    ++checks; try { clean_source(s,l); } catch (const std::exception&) { return; }
    throw std::runtime_error("expected rejection: "+s);
}
int main() {
 try {
    for (const auto& e:language_extensions()) {
        ++checks;
        if (language_for_path("dir/file."+std::string(e.extension))!=e.language) throw std::runtime_error("registry mismatch");
    }
    for (auto ext:{"rs","cs","m","mm","jsx","tsx","txt"}) {
        ++checks; if (language_for_path(std::string("file.")+ext)) throw std::runtime_error("removed extension selected");
    }
    fixture(Language::C,"/\\\n/k secret\nint x;", "\n\nint x;",1);
    fixture(Language::Cpp,"/\\\r\n*k secret k*\\\r\n/ int x;", "\r\n\r\n int x;",1);
    unchanged(Language::Cpp,R"x(u8R"TAG(/*k raw k*/ //k literal)TAG")x");
    rejects(Language::Cpp,"R\"(raw\\\ntext)\"");
    rejects(Language::C,"?" "?/\n/k note");
    unchanged(Language::Cpp,"#include <folder//k-header.h>\n");
    fixture(Language::Go,"//k note\\\nvar x = 1\n", "\nvar x = 1\n",1);
    fixture(Language::Go,"`raw\\` //k removed\n", "`raw\\` \n",1);
    unchanged(Language::Go,"`/*k raw k*/\n//k raw` + \"//k quoted\"");
    fixture(Language::Java,"//k note\\\nint x;\n", "\nint x;\n",1);
    fixture(Language::Java,R"x(\u002f\u002fk secret\u000aint x;)x", R"x(\u000aint x;)x",1);
    fixture(Language::Java,R"x(\u002f\u002ak secret k\u002a\u002f int x;)x", " int x;",1);
    unchanged(Language::Java,R"x("\u002f\u002fk literal" + "\\u002f")x");
    unchanged(Language::Java,"\"\"\"\n//k text\n\\\"\"\" /*k still text k*/\n\"\"\"");
    fixture(Language::Java,"\"\"\"\n//k text\n\"\"\"; //k removed\n", "\"\"\"\n//k text\n\"\"\"; \n",1);
    rejects(Language::Java,R"x(\u00xz)x");
    fixture(Language::Python,"x=1 #k note\r\n# ordinary\r\n", "x=1 \r\n# ordinary\r\n",1);
    fixture(Language::Python,"#k note\\\nprint(1)\n", "\nprint(1)\n",1);
    unchanged(Language::Python,"\"continued\\\r\n#k literal\"\r\n");
    unchanged(Language::Python,"x // 2\n'//k'\n\"#k\"\nr'\\\"#k'\nb'#k'\n'''#k triple\ntext'''\n");
    unchanged(Language::Python,"f\"#k literal {data['#k']} {{#k}} {value:#k}\"");
    fixture(Language::Python,"f\"{value #k note\n}\"", "f\"{value \n}\"",1);
    fixture(Language::Python,"f\"{f'{value #k nested\n}'}\"", "f\"{f'{value \n}'}\"",1);
    fixture(Language::Python,"f\"{value:{width #k width\n}}\"", "f\"{value:{width \n}}\"",1);
    unchanged(Language::Python,"f\"{value!r:>{width}}\"");
    unchanged(Language::Python,"f\"\\N{SNOWMAN} #k\"");
    fixture(Language::Python,"rf\"\\{value #k raw field\n}\"", "rf\"\\{value \n}\"",1);
    rejects(Language::Python,"f\"{value\"");
    rejects(Language::Python,"t\"{value}\"");
    for (auto l:{Language::JavaScript,Language::TypeScript}) {
        unchanged(l,"const re = /[/*k]/g;");
        unchanged(l,"\"continued\\\r\n//k literal\";\r\n");
        unchanged(l,"if (ok) /[/*k]/.test(x);");
        unchanged(l,"while ((ok)) /[/*k]/.test(x);");
        unchanged(l,"const n = a / b / c;");
        unchanged(l,"obj.return / 2; obj.if(x) / 2;");
        rejects(l,"await /[/*k]/;");
        rejects(l,"for await (const x of xs) /[/*k]/.test(x);");
        rejects(l,"break label\n/[/*k]/.test(x);");
        rejects(l,"<!-- //k legacy\n");
        unchanged(l,"const n = /abc/.test(x) / 2;");
        unchanged(l,"const re = /* ordinary */ /[/*k]/;");
        fixture(l,"//k note\\\nrun();", "\nrun();",1);
        fixture(l,"`literal //k ${value /*k private k*/}`", "`literal //k ${value }`",1);
        fixture(l,"`outer ${`inner ${value //k note\n}`} tail`", "`outer ${`inner ${value \n}`} tail`",1);
        fixture(l,"`text ${({key: '//k'}) /*k note k*/}`", "`text ${({key: '//k'}) }`",1);
        unchanged(l,"`escaped \\` //k text \\${ /*k text k*/ }`");
        unchanged(l,"`template ${ /[/*k]/.test(x) }`");
        rejects(l,"if (x) {} /[/*k]/.test(x);");
        rejects(l,"const x = /[[a]--[b]]/v;");
        rejects(l,"`unterminated ${value");
    }
    rejects(Language::TypeScript,"value! / divisor /*k note k*/ 2;");
    // Deterministic malformed-input stress: successful outputs must remain stable.
    std::mt19937 random(42017);
    const std::string alphabet="abc012 \t\r\n/\\*k#'\"`(){}[]:;$+-";
    for (auto language:{Language::C,Language::Cpp,Language::Java,Language::Go,Language::Python,Language::JavaScript,Language::TypeScript}) {
        for (int n=0;n<1500;++n) {
            std::string input;
            for (unsigned length=random()%120; length; --length) input+=alphabet[random()%alphabet.size()];
            CleanResult result;
            try { result=clean_source(input,language); } catch (const std::runtime_error&) { continue; }
            ++checks;
            if (clean_source(result.output,language).output!=result.output)
                throw std::runtime_error("stress idempotence failure: "+input);
            if (std::count(input.begin(),input.end(),'\n')!=std::count(result.output.begin(),result.output.end(),'\n'))
                throw std::runtime_error("stress newline failure");
        }
    }
    std::cout << checks << " language checks passed\n";
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
