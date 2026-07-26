#pragma once

#include <string>
#include <vector>

namespace snapllm {

std::string daemon_pid_path();
bool daemon_start(const std::string& executable, const std::vector<std::string>& args,
                  std::string& error);
bool daemon_stop(std::string& error);
bool daemon_status(std::string& status);

class DaemonChildGuard {
public:
    DaemonChildGuard();
    ~DaemonChildGuard();
    DaemonChildGuard(const DaemonChildGuard&) = delete;
    DaemonChildGuard& operator=(const DaemonChildGuard&) = delete;
};

} // namespace snapllm
