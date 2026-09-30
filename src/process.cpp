#include "kukri/process.hpp"
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <cerrno>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
namespace kukri {
std::string path_text(const std::filesystem::path& p) {
    auto s = p.generic_u8string(); return {s.begin(),s.end()};
}
std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    return out + "'";
}
#ifdef _WIN32
static std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("invalid UTF-8 process argument");
    std::wstring w(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n); return w;
}
// The Windows CRT treats backslashes before quotes differently from ordinary
// backslashes. Double trailing backslashes before adding the closing quote.
static std::wstring windows_quote(const std::string& s) {
    std::wstring out = L"\""; std::size_t slashes = 0;
    for (wchar_t c : wide(s)) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
        if (c == L'"') out += L'\\';
        out += c;
    }
    out.append(slashes*2,L'\\'); return out + L'"';
}
#endif
ProcessResult run(const std::vector<std::string>& args, const std::string& input) {
    if (args.empty()) throw std::runtime_error("empty process command");
    for (const auto& a : args) if (a.find('\0') != a.npos) throw std::runtime_error("NUL in process argument");
    // Anonymous temporary streams avoid pipe deadlocks even for large index blobs.
    using File = std::unique_ptr<FILE, decltype(&std::fclose)>;
    auto temporary = []() -> FILE* {
#ifdef _WIN32
        FILE* f = nullptr; if (tmpfile_s(&f)) return nullptr; return f;
#else
        return std::tmpfile();
#endif
    };
    File in(temporary(), &std::fclose), out(temporary(), &std::fclose), err(temporary(), &std::fclose);
    if (!in || !out || !err) throw std::runtime_error("cannot create subprocess temporary streams");
    if (std::fwrite(input.data(),1,input.size(),in.get()) != input.size()) throw std::runtime_error("cannot write subprocess input");
    std::rewind(in.get());
    int code;
#ifdef _WIN32
    HANDLE handles[3];
    FILE* streams[] = {in.get(),out.get(),err.get()};
    for (int i=0;i<3;++i) {
        handles[i] = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(streams[i])));
        if (!SetHandleInformation(handles[i],HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT)) throw std::runtime_error("cannot inherit process streams");
    }
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput=handles[0]; si.hStdOutput=handles[1]; si.hStdError=handles[2];
    PROCESS_INFORMATION pi{};
    std::wstring command;
    for (const auto& a : args) { if (!command.empty()) command += L' '; command += windows_quote(a); }
    if (!CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))
        throw std::runtime_error("cannot execute " + args[0] + " (Windows error " + std::to_string(GetLastError()) + ")");
    CloseHandle(pi.hThread); WaitForSingleObject(pi.hProcess,INFINITE);
    DWORD status=1; GetExitCodeProcess(pi.hProcess,&status); CloseHandle(pi.hProcess); code=static_cast<int>(status);
#else
    std::vector<char*> argv; for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str())); argv.push_back(nullptr);
    auto pid = fork();
    if (pid < 0) throw std::runtime_error("fork failed");
    if (!pid) {
        if (dup2(fileno(in.get()),0)<0 || dup2(fileno(out.get()),1)<0 || dup2(fileno(err.get()),2)<0) _exit(126);
        // _exit avoids flushing copies of the parent's buffered streams.
        execvp(argv[0],argv.data()); _exit(127);
    }
    int status; while (waitpid(pid,&status,0)<0) { if (errno != EINTR) throw std::runtime_error("waitpid failed"); }
    code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
#endif
    auto read = [](FILE* f) {
        std::rewind(f); std::string data; char buf[8192]; std::size_t n;
        while ((n=std::fread(buf,1,sizeof(buf),f))) data.append(buf,n);
        if (std::ferror(f)) throw std::runtime_error("cannot read subprocess output");
        return data;
    };
    return {code,read(out.get()),read(err.get())};
}
std::filesystem::path executable_path() {
#ifdef _WIN32
    std::wstring buf(32768,0); auto n=GetModuleFileNameW(nullptr,buf.data(),static_cast<DWORD>(buf.size()));
    if (!n || n>=buf.size()) throw std::runtime_error("cannot discover executable path");
    buf.resize(n); return std::filesystem::path(buf);
#elif defined(__APPLE__)
    uint32_t n=0; _NSGetExecutablePath(nullptr,&n); std::string buf(n,0);
    if (_NSGetExecutablePath(buf.data(),&n)) throw std::runtime_error("cannot discover executable path");
    return std::filesystem::canonical(buf.c_str());
#else
    return std::filesystem::canonical("/proc/self/exe");
#endif
}
}
