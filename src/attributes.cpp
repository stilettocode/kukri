#include "kukri/attributes.hpp"
#include <stdexcept>
namespace kukri {
static constexpr auto begin_marker = "# BEGIN kukri managed rules";
static constexpr auto end_marker = "# END kukri managed rules";
std::string attribute_block(std::string_view nl) {
    std::string b = begin_marker; b += nl;
    for (const auto& e : extensions) b += "*." + e + " filter=kukri" + std::string(nl);
    return b + end_marker + std::string(nl);
}
// Match complete marker lines only. Reject ambiguity instead of risking edits
// to unrelated attributes or silently accepting multiple managed sections.
static std::pair<std::size_t,std::size_t> locate(const std::string& text) {
    std::size_t begin=text.npos, end=text.npos;
    for (std::size_t p=0;p<text.size();) {
        auto next=text.find('\n',p); auto stop=next==text.npos?text.size():next;
        auto line=text.substr(p,stop-p); if (!line.empty() && line.back()=='\r') line.pop_back();
        if (line==begin_marker) {
            if (begin!=text.npos || end!=text.npos) throw std::runtime_error("duplicate/malformed kukri attribute block; repair info/attributes manually");
            begin=p;
        }
        if (line==end_marker) {
            if (begin==text.npos || end!=text.npos) throw std::runtime_error("unmatched kukri attribute delimiter; repair info/attributes manually");
            end=next==text.npos?text.size():next+1;
        }
        p=next==text.npos?text.size():next+1;
    }
    if (begin!=text.npos && end==text.npos) throw std::runtime_error("unterminated kukri attribute block; repair info/attributes manually");
    return {begin,end};
}
std::string update_attributes(const std::string& text, bool enable) {
    auto [begin,end]=locate(text);
    auto nl=text.find("\r\n")!=text.npos?"\r\n":"\n";
    auto block=enable?attribute_block(nl):"";
    if (begin!=text.npos) return text.substr(0,begin)+block+text.substr(end);
    if (!enable) return text;
    return text + (!text.empty() && text.back()!='\n'?nl:"") + block;
}
bool attributes_healthy(const std::string& text) {
    auto [begin,end]=locate(text);
    if (begin==text.npos) return false;
    auto b=text.substr(begin,end-begin);
    return b==attribute_block() || b==attribute_block("\r\n");
}
}
