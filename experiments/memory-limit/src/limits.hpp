// limits.hpp - recipe for swarm: one memory cgroup for the program and its
// engine, and an engine process that cannot outlive the program.
//
//   enter_limited_scope() - start inside a user systemd scope with
//                           MemoryMax and MemorySwapMax=0, without root;
//   read_memory()         - limit and usage of the group, from the inside;
//   spawn_bound()         - start rpc-server / llama-server bound to us
//                           with PR_SET_PDEATHSIG;
//   stop_bound()          - graceful stop: SIGTERM, timeout, SIGKILL.
#pragma once

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <linux/close_range.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace swarm::limits {

constexpr uint64_t kNoLimit = UINT64_MAX;  // "max" in cgroup files

inline std::optional<std::string> read_text(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// One value: a number or "max". Empty if the file is missing.
inline std::optional<uint64_t> read_value(const std::string& path) {
    const auto text = read_text(path);
    if (!text) return std::nullopt;
    std::istringstream in(*text);
    std::string word;
    in >> word;
    if (word == "max") return kNoLimit;
    try {
        return std::stoull(word);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// "key value" lines: memory.events, memory.stat.
inline std::map<std::string, uint64_t> read_pairs(const std::string& path) {
    std::map<std::string, uint64_t> pairs;
    std::istringstream in(read_text(path).value_or(""));
    std::string key;
    uint64_t value = 0;
    while (in >> key >> value) pairs[key] = value;
    return pairs;
}

// cgroup v2 directory of a process, for example
// /sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service/app.slice/swarm-1.scope
inline std::string cgroup_dir(const std::string& pid = "self") {
    std::string group;
    std::istringstream membership(read_text("/proc/" + pid + "/cgroup").value_or(""));
    for (std::string line; std::getline(membership, line);)
        if (line.rfind("0::", 0) == 0) group = line.substr(3);
    if (group.empty()) throw std::runtime_error("process " + pid + " is not in a cgroup v2 hierarchy");
    std::istringstream mounts(read_text("/proc/self/mountinfo").value_or(""));
    for (std::string line; std::getline(mounts, line);) {
        const auto separator = line.find(" - cgroup2 ");
        if (separator == std::string::npos) continue;
        std::istringstream fields(line.substr(0, separator));
        std::string id, parent, device, root, mountpoint;
        fields >> id >> parent >> device >> root >> mountpoint;
        if (root != "/" && group.rfind(root, 0) == 0) group.erase(0, root.size());
        return mountpoint + group;
    }
    throw std::runtime_error("cgroup2 is not mounted");
}

struct Memory {
    std::string dir;                         // cgroup directory
    uint64_t max = kNoLimit;                 // memory.max
    uint64_t swap_max = kNoLimit;            // memory.swap.max
    uint64_t current = 0;                    // memory.current: all charged memory, page cache included
    std::optional<uint64_t> peak;            // memory.peak: maximum of current since the group was created
    std::optional<uint64_t> swap_current;    // memory.swap.current
    std::optional<uint64_t> swap_peak;       // memory.swap.peak
    std::map<std::string, uint64_t> events;  // memory.events: max, oom, oom_kill, oom_group_kill
    std::map<std::string, uint64_t> stat;    // memory.stat: anon, file, file_mapped, ...

    uint64_t anon() const { return get(stat, "anon"); }        // heap, buffers, KV cache
    uint64_t file() const { return get(stat, "file"); }        // page cache, mmap of GGUF included
    uint64_t oom_kills() const { return get(events, "oom_kill"); }

    static uint64_t get(const std::map<std::string, uint64_t>& values, const char* key) {
        const auto it = values.find(key);
        return it == values.end() ? 0 : it->second;
    }
};

inline Memory read_memory(const std::string& dir) {
    Memory memory;
    memory.dir = dir;
    memory.max = read_value(dir + "/memory.max").value_or(kNoLimit);
    memory.swap_max = read_value(dir + "/memory.swap.max").value_or(kNoLimit);
    memory.current = read_value(dir + "/memory.current").value_or(0);
    memory.peak = read_value(dir + "/memory.peak");
    memory.swap_current = read_value(dir + "/memory.swap.current");
    memory.swap_peak = read_value(dir + "/memory.swap.peak");
    memory.events = read_pairs(dir + "/memory.events");
    memory.stat = read_pairs(dir + "/memory.stat");
    return memory;
}

inline std::string limit_text(uint64_t value) {
    return value == kNoLimit ? "max" : std::to_string(value);
}

// Ensures the process runs in a user systemd scope with MemoryMax=limit_bytes
// and MemorySwapMax=0. Otherwise the process re-executes itself through
// `systemd-run --user --scope`: systemd-run moves its own PID into a new scope
// and then execs this binary with the same arguments, so the PID, terminal,
// environment and working directory stay the same. Call it first in main(),
// before printing anything or starting threads. Returns only when the limits
// are in place; otherwise throws, and the program must not continue.
inline Memory enter_limited_scope(char** argv, uint64_t limit_bytes, const std::string& unit_prefix) {
    static constexpr const char* kMarker = "SWARM_LIMIT_REEXEC";
    const Memory memory = read_memory(cgroup_dir());
    if (memory.max == limit_bytes && memory.swap_max == 0) {
        unsetenv(kMarker);
        return memory;
    }
    if (std::getenv(kMarker) != nullptr)
        throw std::runtime_error("systemd-run did not apply the limit: memory.max=" + limit_text(memory.max) +
                                 ", memory.swap.max=" + limit_text(memory.swap_max));
    if (std::getenv("XDG_RUNTIME_DIR") == nullptr && std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr)
        throw std::runtime_error("no user systemd: XDG_RUNTIME_DIR and DBUS_SESSION_BUS_ADDRESS are not set");

    char exe[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (length <= 0) throw std::system_error(errno, std::generic_category(), "readlink /proc/self/exe");
    exe[length] = '\0';

    std::vector<std::string> command = {
        "systemd-run", "--user", "--scope", "--quiet", "--collect",
        "--unit=" + unit_prefix + std::to_string(getpid()) + "-" + std::to_string(std::time(nullptr)) + ".scope",
        "--property=MemoryMax=" + std::to_string(limit_bytes),
        "--property=MemorySwapMax=0",
        // The default for scopes is OOMPolicy=stop: after the kernel kills the
        // engine, systemd would stop the whole scope, the program included.
        "--property=OOMPolicy=continue",
        "--", exe};
    for (int i = 1; argv[i] != nullptr; ++i) command.emplace_back(argv[i]);
    std::vector<char*> args;
    for (auto& word : command) args.push_back(word.data());
    args.push_back(nullptr);

    setenv(kMarker, "1", 1);
    execvp(args[0], args.data());
    const int error = errno;
    unsetenv(kMarker);
    throw std::system_error(error, std::generic_category(), "exec systemd-run");
}

// Starts argv[0] (absolute path) as a child that the kernel kills with SIGKILL
// as soon as the calling *thread* exits, including kill -9 of the program.
// Call it from a thread that lives as long as the process, e.g. the event loop.
// The child stays in our cgroup (fork inherits it, exec keeps it), gets its own
// process group (terminal Ctrl+C reaches only the program), starts with an
// empty signal mask and default SIGINT/SIGTERM/SIGHUP/SIGPIPE, stdin=/dev/null,
// stdout and stderr = log_fd (or ours when -1), and no other descriptors.
// Returns after a successful exec; throws if the program cannot be started.
inline pid_t spawn_bound(const std::vector<std::string>& argv, int log_fd = -1) {
    if (argv.empty() || argv[0].empty() || argv[0][0] != '/')
        throw std::invalid_argument("spawn_bound: absolute path of the executable is required");
    std::vector<char*> args;
    for (const auto& word : argv) args.push_back(const_cast<char*>(word.c_str()));
    args.push_back(nullptr);

    int report[2];  // the child writes errno here if exec fails; EOF = exec succeeded
    if (pipe2(report, O_CLOEXEC) != 0) throw std::system_error(errno, std::generic_category(), "pipe2");
    const pid_t parent = getpid();
    const pid_t pid = fork();
    if (pid < 0) {
        const int error = errno;
        close(report[0]);
        close(report[1]);
        throw std::system_error(error, std::generic_category(), "fork");
    }
    if (pid == 0) {
        // Child: only async-signal-safe calls until exec.
        auto fail = [&](int code) {
            const int error = errno;
            (void)!write(report[1], &error, sizeof error);
            _exit(code);
        };
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) fail(126);
        if (getppid() != parent) _exit(126);  // the program died before prctl: do not start
        setpgid(0, 0);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        struct sigaction defaults {};
        defaults.sa_handler = SIG_DFL;
        for (const int signal_number : {SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGQUIT})
            sigaction(signal_number, &defaults, nullptr);
        const int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd < 0 || dup2(null_fd, STDIN_FILENO) < 0) fail(126);
        if (log_fd >= 0 && (dup2(log_fd, STDOUT_FILENO) < 0 || dup2(log_fd, STDERR_FILENO) < 0)) fail(126);
        close_range(3, ~0U, CLOSE_RANGE_CLOEXEC);  // sockets and files of the program stay with it
        execv(args[0], args.data());
        fail(127);
    }
    close(report[1]);
    int child_error = 0;
    ssize_t received;
    do {
        received = read(report[0], &child_error, sizeof child_error);
    } while (received < 0 && errno == EINTR);
    close(report[0]);
    if (received == static_cast<ssize_t>(sizeof child_error)) {
        waitpid(pid, nullptr, 0);
        throw std::system_error(child_error, std::generic_category(), "start " + argv[0]);
    }
    return pid;
}

// SIGTERM to the child's process group, wait up to timeout_ms, then SIGKILL.
// Returns the wait status of the child.
inline int stop_bound(pid_t pid, int timeout_ms = 3000) {
    kill(-pid, SIGTERM);
    int status = 0;
    for (int waited = 0; waited < timeout_ms; waited += 20) {
        const pid_t reaped = waitpid(pid, &status, WNOHANG);
        if (reaped == pid || (reaped < 0 && errno == ECHILD)) return status;
        usleep(20 * 1000);
    }
    kill(-pid, SIGKILL);
    waitpid(pid, &status, 0);
    return status;
}

}  // namespace swarm::limits
