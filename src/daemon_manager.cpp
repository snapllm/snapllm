#include "snapllm/daemon_manager.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace snapllm {
namespace {
fs::path state_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("LOCALAPPDATA");
    return appdata ? fs::path(appdata) / "SnapLLM" : fs::temp_directory_path() / "SnapLLM";
#else
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime) return fs::path(runtime) / "snapllm";
    const char* home = std::getenv("HOME");
    return home ? fs::path(home) / ".local" / "state" / "snapllm" : fs::temp_directory_path() / "snapllm";
#endif
}
bool read_pid(unsigned long& pid) {
    std::ifstream in(daemon_pid_path());
    return static_cast<bool>(in >> pid);
}
}

std::string daemon_pid_path() {
    return (state_dir() / "snapllm.pid").string();
}

bool daemon_start(const std::string& executable, const std::vector<std::string>& args,
                  std::string& error) {
    unsigned long existing = 0;
    if (read_pid(existing)) {
#ifdef _WIN32
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, existing);
        if (process) { CloseHandle(process); error = "SnapLLM daemon is already running"; return false; }
#else
        if (kill(static_cast<pid_t>(existing), 0) == 0) { error = "SnapLLM daemon is already running"; return false; }
#endif
    }
    std::error_code ec;
    fs::create_directories(state_dir(), ec);
    if (ec) { error = "Cannot create daemon state directory: " + ec.message(); return false; }
#ifdef _WIN32
    std::string command = "\"" + executable + "\"";
    for (const auto& arg : args) command += " \"" + arg + "\"";
    STARTUPINFOA startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &startup, &process)) {
        error = "CreateProcess failed: " + std::to_string(GetLastError()); return false;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
#else
    const pid_t child = fork();
    if (child < 0) { error = std::strerror(errno); return false; }
    if (child == 0) {
        if (setsid() < 0) _exit(127);
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) { dup2(null_fd, STDIN_FILENO); dup2(null_fd, STDOUT_FILENO); dup2(null_fd, STDERR_FILENO); close(null_fd); }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
#endif
    return true;
}

bool daemon_stop(std::string& error) {
    unsigned long pid = 0;
    if (!read_pid(pid)) { error = "SnapLLM daemon is not running"; return false; }
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!process) { fs::remove(daemon_pid_path()); error = "SnapLLM daemon is not running"; return false; }
    const bool ok = TerminateProcess(process, 0) != 0; CloseHandle(process);
#else
    const bool ok = kill(static_cast<pid_t>(pid), SIGTERM) == 0;
    if (!ok && errno == ESRCH) fs::remove(daemon_pid_path());
#endif
    if (!ok) { error = "Failed to stop SnapLLM daemon"; return false; }
    return true;
}

bool daemon_status(std::string& status) {
    unsigned long pid = 0;
    if (!read_pid(pid)) { status = "stopped"; return false; }
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) { fs::remove(daemon_pid_path()); status = "stopped"; return false; }
    CloseHandle(process);
#else
    if (kill(static_cast<pid_t>(pid), 0) != 0) { fs::remove(daemon_pid_path()); status = "stopped"; return false; }
#endif
    status = "running (pid " + std::to_string(pid) + ")"; return true;
}

DaemonChildGuard::DaemonChildGuard() {
    std::error_code ec; fs::create_directories(state_dir(), ec);
    std::ofstream(daemon_pid_path(), std::ios::trunc) <<
#ifdef _WIN32
        GetCurrentProcessId();
#else
        getpid();
#endif
}
DaemonChildGuard::~DaemonChildGuard() { std::error_code ec; fs::remove(daemon_pid_path(), ec); }
} // namespace snapllm
