#include "kukri/git.hpp"
#include "kukri/attributes.hpp"
#include "kukri/cleaner.hpp"
#include "kukri/process.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace kukri {
namespace fs=std::filesystem;
static fs::path utf8path(const std::string& s) { return fs::path(std::u8string(s.begin(),s.end())); }
static ProcessResult git(std::vector<std::string> args, const std::string& input={}) {
    args.insert(args.begin(),"git"); return run(args,input);
}
static std::string checked(std::vector<std::string> args, const std::string& input={}) {
    auto r=git(std::move(args),input);
    if (r.code) throw std::runtime_error("Git command failed (exit "+std::to_string(r.code)+"): "+r.err);
    return r.out;
}
static std::string trim_eol(std::string s) { while (!s.empty() && (s.back()=='\n' || s.back()=='\r')) s.pop_back(); return s; }
static std::string config(const std::string& key, bool local=false) {
    std::vector<std::string> args={"config"}; if (local) args.push_back("--local");
    args.insert(args.end(),{"--get-all",key}); auto r=git(args);
    if (r.code==1) return {};
    if (r.code) throw std::runtime_error("cannot read Git configuration: "+r.err);
    return trim_eol(r.out);
}
static std::string read_file(const fs::path& path) {
    if (!fs::exists(path)) return {};
    std::ifstream f(path,std::ios::binary); if (!f) throw std::runtime_error("cannot read "+path_text(path));
    std::string s{std::istreambuf_iterator<char>(f),{}}; if (f.bad()) throw std::runtime_error("cannot read attributes"); return s;
}
static void write_file(const fs::path& path, const std::string& s) {
    if (fs::is_symlink(fs::symlink_status(path))) throw std::runtime_error("refusing to replace symlink attributes file");
    fs::create_directories(path.parent_path());
    // Reserve the replacement file exclusively. This does not serialize the
    // earlier read/config updates; callers must avoid concurrent enable/disable.
    auto lock=path; lock += ".kukri-lock";
    if (!fs::create_directory(lock)) throw std::runtime_error("attributes lock exists: "+path_text(lock));
    auto temp=lock/"attributes";
    try {
        { std::ofstream f(temp,std::ios::binary); f.write(s.data(),static_cast<std::streamsize>(s.size())); f.close(); if (!f) throw std::runtime_error("cannot write attributes"); }
#ifdef _WIN32
        if (!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("cannot replace attributes");
#else
        fs::rename(temp,path);
#endif
        fs::remove(lock);
    } catch (...) { std::error_code ec; fs::remove(temp,ec); fs::remove(lock,ec); throw; }
}
static void unset(const std::string& key) {
    auto r=git({"config","--local","--unset-all",key});
    if (r.code!=0 && r.code!=5) throw std::runtime_error("cannot remove "+key+": "+r.err);
}
static bool relevant(const std::string& p) { return language_for_path(p).has_value(); }
// NUL-delimited plumbing preserves spaces, tabs, and newlines in tracked paths.
static std::string nul_field(const std::string& data, std::size_t& pos) {
    auto end=data.find('\0',pos);
    if (end==data.npos) throw std::runtime_error("malformed NUL-delimited Git output");
    auto field=data.substr(pos,end-pos); pos=end+1; return field;
}
static bool audit_tracked_filters() {
    auto entries=checked({"ls-files","--stage","-z"});
    std::set<std::string> paths;
    for (std::size_t pos=0;pos<entries.size();) {
        auto entry=nul_field(entries,pos);
        auto tab=entry.find('\t'), space=entry.find(' ');
        if (tab==entry.npos || space==entry.npos || space>=tab)
            throw std::runtime_error("malformed index entry during attribute audit");
        auto mode=entry.substr(0,space), path=entry.substr(tab+1);
        if ((mode=="100644" || mode=="100755") && relevant(path)) paths.insert(path);
    }
    std::string input;
    for (const auto& path:paths) { input+=path; input+='\0'; }
    std::size_t failures=0;
    if (!paths.empty()) {
        // Use working-tree attributes, just as the next normal git add will.
        auto output=checked({"check-attr","-z","--stdin","filter"},input);
        std::size_t pos=0;
        for (const auto& expected:paths) {
            auto path=nul_field(output,pos);
            auto attribute=nul_field(output,pos);
            auto value=nul_field(output,pos);
            if (path!=expected || attribute!="filter")
                throw std::runtime_error("unexpected Git attribute response");
            if (value!="kukri") {
                ++failures;
                std::cerr << "[FAIL] " << std::quoted(path) << ": filter=" << std::quoted(value)
                          << " (expected kukri)\n";
            }
        }
        if (pos!=output.size()) throw std::runtime_error("extra Git attribute response");
    }
    std::cout << paths.size() << " tracked source paths checked; " << failures << " not assigned to kukri.\n";
    if (failures) std::cerr << "Review overriding filter rules in info/attributes and .gitattributes; then rerun kukri doctor.\n";
    if (paths.empty()) std::cout << "No tracked source paths to audit; untracked files are not checked.\n";
    return failures==0;
}
// Accept only the exact command format we generate, never arbitrary shell syntax.
static fs::path configured_executable(std::string command) {
    if (command.ends_with(" --path %f")) command.resize(command.size()-10);
    if (!command.starts_with("'") || !command.ends_with("' clean")) return {};
    auto quoted=command.substr(1,command.size()-8);
    std::string path;
    for (std::size_t i=0;i<quoted.size();) {
        if (quoted.compare(i,4,"'\\''")==0) { path+='\''; i+=4; }
        else { if (quoted[i]=='\'') return {}; path+=quoted[i++]; }
    }
    auto p=utf8path(path);
    if (!p.is_absolute() || shell_quote(path)+" clean"!=command) return {};
    return p;
}
static bool same_executable(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return !a.empty() && fs::equivalent(a,b,ec) && !ec;
}
int repository_command(const std::string& command, const std::string& executable) {
    auto running=executable_path();
    auto selected=executable.empty()?fs::path{}:utf8path(executable);
    if (!executable.empty() && (!selected.is_absolute() || !same_executable(selected,running)))
        throw std::runtime_error("--executable must be an absolute path to this running executable (a symlink is allowed); run the installed binary to enable it");
    checked({"--version"});
    if (trim_eol(checked({"rev-parse","--is-inside-work-tree"}))!="true") throw std::runtime_error("current directory is not inside a Git working tree");
    auto root=trim_eol(checked({"rev-parse","--show-toplevel"}));
    auto dir=trim_eol(checked({"rev-parse","--absolute-git-dir"}));
    // Git resolves common-directory info/attributes for linked worktrees itself.
    auto attr=utf8path(trim_eol(checked({"rev-parse","--path-format=absolute","--git-path","info/attributes"})));
    fs::current_path(utf8path(root));
    auto original=read_file(attr);
    auto configured=configured_executable(config("filter.kukri.clean"));
    // Keep a working configured link on subsequent enable calls and in diagnostics.
    auto exe=!selected.empty()?selected:(same_executable(configured,running)?configured:running);
    auto expected=shell_quote(path_text(exe))+" clean --path %f";
    auto identity=shell_quote(path_text(exe))+" smudge";
    if (command=="enable" || command=="disable") {
        auto updated=update_attributes(original,command=="enable");
        auto old_clean=config("filter.kukri.clean",true), old_required=config("filter.kukri.required",true), old_smudge=config("filter.kukri.smudge",true);
        if (old_clean.find('\n')!=old_clean.npos || old_required.find('\n')!=old_required.npos || old_smudge.find('\n')!=old_smudge.npos)
            throw std::runtime_error("multiple kukri config values; repair local configuration before enabling/disabling");
        auto active_clean=config("filter.kukri.clean"), active_smudge=config("filter.kukri.smudge");
        auto prior_path=configured_executable(active_clean);
        bool prior_identity=!prior_path.empty() && active_smudge==shell_quote(path_text(prior_path))+" smudge";
        if (command=="enable" && (!config("filter.kukri.process").empty() || (!active_smudge.empty() && active_smudge!=identity && !prior_identity)))
            throw std::runtime_error("conflicting filter.kukri.process or smudge configuration; remove it before enabling");
        try {
            if (command=="enable") {
                checked({"config","--local","--replace-all","filter.kukri.required","true"});
                checked({"config","--local","--replace-all","filter.kukri.clean",expected});
                checked({"config","--local","--replace-all","filter.kukri.smudge",identity});
                if (config("filter.kukri.clean")!=expected || config("filter.kukri.required")!="true" || config("filter.kukri.smudge")!=identity)
                    throw std::runtime_error("higher-priority configuration overrides kukri; repair worktree/environment configuration");
                if (updated!=original) write_file(attr,updated);
            } else {
                // Remove managed references before the driver to avoid a dangling required filter.
                if (updated!=original) write_file(attr,updated);
                unset("filter.kukri.clean"); unset("filter.kukri.required"); unset("filter.kukri.smudge");
            }
        } catch (...) {
            // Git config and attribute replacement are separate operations. Restore
            // the previous state where possible and report a failed rollback.
            try {
                if (old_clean.empty()) unset("filter.kukri.clean"); else checked({"config","--local","--replace-all","filter.kukri.clean",old_clean});
                if (old_smudge.empty()) unset("filter.kukri.smudge"); else checked({"config","--local","--replace-all","filter.kukri.smudge",old_smudge});
                if (old_required.empty()) unset("filter.kukri.required"); else checked({"config","--local","--replace-all","filter.kukri.required",old_required});
                if (read_file(attr)!=original) write_file(attr,original);
            } catch (const std::exception& e) { std::cerr << "kukri: rollback failed: " << e.what() << "; run kukri doctor\n"; }
            throw;
        }
        if (command=="enable") std::cout << "kukri is enabled for this repository.\nExisting index entries are not automatically rewritten. When ready, run:\n  git add --renormalize .\nReview git diff --cached afterward.\n";
        else std::cout << "kukri is disabled locally. Working files and index contents were not changed.\n";
        return 0;
    }
    bool clean=config("filter.kukri.clean")==expected;
    auto required_result=git({"config","--type=bool","--get","filter.kukri.required"});
    bool required=required_result.code==0 && trim_eol(required_result.out)=="true";
    bool attrs=attributes_healthy(original);
    bool conflict=!config("filter.kukri.process").empty() || config("filter.kukri.smudge")!=identity;
    bool healthy=clean && required && attrs && !conflict;
    if (command=="status" || command=="doctor") {
        std::cout << "Repository: " << root << "\nGit directory: " << dir << "\nAttributes: " << path_text(attr)
            << "\nExecutable: " << path_text(exe) << "\nEnabled: " << (healthy?"yes":"no / incomplete")
            << "\nClean command: " << config("filter.kukri.clean") << "\nRequired: " << (required?"yes":"no")
            << "\nManaged rules (" << extensions.size() << "): " << (attrs?"valid":"missing / altered") << '\n';
        if (!healthy) std::cout << "Run kukri enable to repair; remove conflicting process/smudge or higher-priority configuration first.\n";
        if (command=="status") return 0;
        bool tracked_healthy=audit_tracked_filters();
        if (!healthy) return 1;
        // hash-object runs the actual configured filter without writing an object or index entry.
        for (const auto& ext : extensions) {
            auto language=*language_for_path("probe."+ext);
            std::string probe=language==Language::Python?"x = 1\n#k doctor probe\ny = 2\n":"int x;\n//k doctor probe\nint y;\n";
            auto wanted=checked({"hash-object","--stdin","--no-filters"},clean_source(probe,language).output);
            auto got=git({"hash-object","--stdin","--path=.kukri-doctor."+ext},probe);
            if (got.code || got.out!=wanted) throw std::runtime_error("configured filter self-test failed for ."+ext+": "+got.err);
        }
        std::cout << "[OK] Git and repository detected\n[OK] Required filter and all configured extension probes passed\nNo source files or index entries modified.\n";
        return tracked_healthy?0:1;
    }
    if (!healthy) std::cerr << "kukri: configuration is incomplete or differs from this executable; run kukri doctor. Inspecting index anyway.\n";
    // Read blobs by object ID, so path bytes cannot become revision syntax.
    // Inspect every regular-file conflict stage, not just stage zero.
    auto entries=checked({"ls-files","--stage","-z"});
    std::size_t count=0; bool leaked=false;
    for (std::size_t pos=0;pos<entries.size();) {
        auto end=entries.find('\0',pos); if (end==entries.npos) throw std::runtime_error("malformed git ls-files output");
        auto entry=entries.substr(pos,end-pos); pos=end+1;
        auto tab=entry.find('\t'), first=entry.find(' '), second=entry.find(' ',first+1);
        if (tab==entry.npos || first==entry.npos || second==entry.npos) throw std::runtime_error("malformed index entry");
        auto path=entry.substr(tab+1), mode=entry.substr(0,first);
        if (!relevant(path) || (mode!="100644" && mode!="100755")) continue;
        ++count;
        auto blob=checked({"cat-file","blob",entry.substr(first+1,second-first-1)});
        try {
            auto result=clean_source(blob,*language_for_path(path));
            for (const auto& token : result.comments) {
                std::cerr << "Private comment in index: " << std::quoted(path) << ':' << token.line << " (stage " << entry.substr(second+1,tab-second-1) << ")\n";
                leaked=true;
            }
        } catch (const std::exception& e) { std::cerr << "Cannot verify " << std::quoted(path) << ": " << e.what() << '\n'; leaked=true; }
    }
    std::cout << count << " relevant index entries inspected.\n";
    if (!leaked) std::cout << "No recognized private comments found in the Git index.\n";
    return leaked?1:0;
}
}
