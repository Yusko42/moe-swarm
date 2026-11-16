// workload - test parent with two children, experiment 1.3.
//
//   workload parent --out DIR [--mib N] [--file PATH] [--pdeathsig]
//                   [--graceful] [--from-thread] [--lifetime SECONDS]
//   workload child  --out DIR --index I [--mib N] [--file PATH] [--lifetime SECONDS]
//
// Each process writes DIR/<role>.json: PID, start time, process group and its
// memory cgroup as seen from the inside. Children wait for DIR/go: the outside
// runner creates it only after it has checked the limit. Then each child
// allocates N MiB of anonymous memory and writes every page; with --file
// child 0 instead maps the file and reads every page twice.
//
// Parent signals: SIGUSR1 - normal exit without stopping the children
// (the worst case for cleanup); SIGINT/SIGTERM - default action, or with
// --graceful the children are stopped first. With --pdeathsig the children
// are started by swarm::limits::spawn_bound, otherwise by plain fork+exec;
// in both cases they get their own process group. With --from-thread they are
// started from a helper thread that exits when DIR/go appears.
#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>

#include <sys/mman.h>
#include <sys/resource.h>

#include "limits.hpp"
#include "report.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using report::Json;

struct Options {
    std::string role;
    std::string out;
    std::string file;
    int index = 0;
    long mib = 0;
    int lifetime = 45;
    bool pdeathsig = false;
    bool graceful = false;
    bool from_thread = false;
};

volatile sig_atomic_t g_signal = 0;
void remember(int signal_number) { g_signal = signal_number; }

bool wait_for(const std::string& path, Clock::time_point deadline) {
    while (Clock::now() < deadline) {
        if (report::exists(path)) return true;
        usleep(20 * 1000);
    }
    return false;
}

std::string status_text(int status) {
    if (WIFEXITED(status)) return "exit " + std::to_string(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return std::string("signal ") + sigabbrev_np(WTERMSIG(status));
    return "status " + std::to_string(status);
}

pid_t spawn_plain(const std::vector<std::string>& argv, int log_fd) {
    std::vector<char*> args;
    for (const auto& word : argv) args.push_back(const_cast<char*>(word.c_str()));
    args.push_back(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        const int null_fd = open("/dev/null", O_RDONLY);
        dup2(null_fd, STDIN_FILENO);
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
        }
        execv(args[0], args.data());
        _exit(127);
    }
    if (pid < 0) throw std::system_error(errno, std::generic_category(), "fork");
    return pid;
}

int run_parent(const Options& o) {
    report::write_file(o.out + "/parent.json", report::identity("parent"));
    struct sigaction action {};
    action.sa_handler = remember;
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR1, &action, nullptr);
    if (o.graceful) {
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGHUP, &action, nullptr);
    }
    const std::string dir = swarm::limits::cgroup_dir();
    const uint64_t oom_kills_at_start = swarm::limits::read_memory(dir).oom_kills();
    const int log_fd = open((o.out + "/children.log").c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    const auto deadline = Clock::now() + std::chrono::seconds(o.lifetime);

    std::vector<pid_t> children(2, -1);
    auto spawn_children = [&] {
        for (int i = 0; i < 2; ++i) {
            std::vector<std::string> argv = {report::self_exe(), "child", "--out", o.out, "--index", std::to_string(i),
                                             "--mib", std::to_string(o.mib), "--lifetime", std::to_string(o.lifetime)};
            if (i == 0 && !o.file.empty()) {
                argv.push_back("--file");
                argv.push_back(o.file);
            }
            children[i] = o.pdeathsig ? swarm::limits::spawn_bound(argv, log_fd) : spawn_plain(argv, log_fd);
        }
    };
    std::promise<void> started;  // outlives the helper thread's set_value()
    if (o.from_thread) {
        // PR_SET_PDEATHSIG fires when the *thread* that forked exits.
        std::thread([&, deadline] {
            spawn_children();
            started.set_value();
            wait_for(o.out + "/go", deadline);
        }).detach();
        started.get_future().wait();
    } else {
        spawn_children();
    }

    std::string exits;
    int alive = 2;
    bool announced = false;
    auto write_exits = [&] { report::write_file(o.out + "/child-exits.json", "[" + exits + "]"); };
    auto record_exit = [&](pid_t pid, int status, const char* how) {
        const auto index = std::find(children.begin(), children.end(), pid) - children.begin();
        if (index >= static_cast<long>(children.size())) return;
        const uint64_t oom_kills = swarm::limits::read_memory(dir).oom_kills();
        if (!exits.empty()) exits += ", ";
        exits += Json()
                     .text("role", "child-" + std::to_string(index))
                     .num("pid", pid)
                     .text("status", status_text(status))
                     .text("how", how)
                     .num("oom_kill_delta", oom_kills - oom_kills_at_start)
                     .str();
        children[index] = -1;
        --alive;
        write_exits();
    };

    while (Clock::now() < deadline) {
        if (g_signal == SIGUSR1) return 0;  // normal exit; children are deliberately left alone
        if (g_signal != 0) {                // --graceful: SIGINT, SIGTERM, SIGHUP
            const int signal_number = g_signal;
            for (const pid_t pid : std::vector<pid_t>(children))
                if (pid > 0) record_exit(pid, swarm::limits::stop_bound(pid), "stopped by parent");
            report::write_file(o.out + "/parent-exit.json",
                               Json().text("signal", sigabbrev_np(signal_number)).num("code", 128 + signal_number).str());
            return 128 + signal_number;
        }
        int status = 0;
        for (pid_t pid; alive > 0 && (pid = waitpid(-1, &status, WNOHANG)) > 0;)
            record_exit(pid, status, "reaped");
        if (!announced && exits.empty() && report::exists(o.out + "/child-0.json") &&
            report::exists(o.out + "/child-1.json")) {
            report::write_file(o.out + "/ready.json", Json().num("pid", getpid()).str());
            announced = true;
        }
        usleep(20 * 1000);
    }
    return 0;
}

void touch_anonymous(const Options& o, const std::string& role, Clock::time_point deadline) {
    const size_t chunk = size_t{8} << 20;
    const size_t target = static_cast<size_t>(o.mib) << 20;
    const long page = sysconf(_SC_PAGESIZE);
    size_t done = 0;
    while (done < target && Clock::now() < deadline) {
        const size_t size = std::min(chunk, target - done);
        auto* block = static_cast<char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (block == MAP_FAILED) break;
        for (size_t offset = 0; offset < size; offset += page) block[offset] = 1;
        done += size;
        usleep(10 * 1000);  // both children grow together
    }
    report::write_file(o.out + "/" + role + "-alloc.json", Json().num("bytes", done).num("target", target).str());
}

void read_file_twice(const Options& o, const std::string& role) {
    const int fd = open(o.file.c_str(), O_RDONLY | O_CLOEXEC);
    struct stat info {};
    if (fd < 0 || fstat(fd, &info) != 0) return;
    // Pages cached by whoever wrote the file are charged to that writer's group.
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    void* mapped = mmap(nullptr, info.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) return;
    const auto* data = static_cast<const volatile char*>(mapped);
    const long page = sysconf(_SC_PAGESIZE);
    Json result;
    result.num("bytes", info.st_size);
    for (int pass = 1; pass <= 2; ++pass) {
        rusage before {}, after {};
        getrusage(RUSAGE_SELF, &before);
        const auto start = Clock::now();
        uint64_t sum = 0;
        for (off_t offset = 0; offset < info.st_size; offset += page) sum += data[offset];
        getrusage(RUSAGE_SELF, &after);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        const auto memory = swarm::limits::read_memory(swarm::limits::cgroup_dir());
        const std::string prefix = "pass" + std::to_string(pass) + "_";
        result.num(prefix + "major_faults", after.ru_majflt - before.ru_majflt)
            .num(prefix + "ms", ms)
            .num(prefix + "sum", sum)
            .raw(prefix + "memory", report::memory(memory));
    }
    report::write_file(o.out + "/" + role + "-file.json", result.str());
}

int run_child(const Options& o) {
    const std::string role = "child-" + std::to_string(o.index);
    report::write_file(o.out + "/" + role + ".json", report::identity(role));
    const auto deadline = Clock::now() + std::chrono::seconds(o.lifetime);
    if (!wait_for(o.out + "/go", deadline)) return 0;
    if (!o.file.empty())
        read_file_twice(o, role);
    else if (o.mib > 0)
        touch_anonymous(o, role, deadline);
    while (Clock::now() < deadline) usleep(50 * 1000);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (argc < 2) {
        std::fprintf(stderr, "usage: workload parent|child --out DIR [options]\n");
        return 2;
    }
    o.role = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (arg == "--out") o.out = next();
        else if (arg == "--file") o.file = next();
        else if (arg == "--index") o.index = std::stoi(next());
        else if (arg == "--mib") o.mib = std::stol(next());
        else if (arg == "--lifetime") o.lifetime = std::stoi(next());
        else if (arg == "--pdeathsig") o.pdeathsig = true;
        else if (arg == "--graceful") o.graceful = true;
        else if (arg == "--from-thread") o.from_thread = true;
        else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            return 2;
        }
    }
    if (o.out.empty()) {
        std::fprintf(stderr, "--out is required\n");
        return 2;
    }
    try {
        if (o.role == "parent") return run_parent(o);
        if (o.role == "child") return run_child(o);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "workload %s: %s\n", o.role.c_str(), error.what());
        return 1;
    }
    std::fprintf(stderr, "unknown role: %s\n", o.role.c_str());
    return 2;
}
