// swarm-demo - how `swarm join` enters its 8 GiB group and keeps rpc-server
// bound to itself. A model of the agent for questions 5 and 6, experiment 1.3.
//
//   swarm-demo [--out DIR] [--limit BYTES] [--child PATH [ARG...]]
//
// 1. enter_limited_scope(): re-executes itself through systemd-run into a
//    scope with MemoryMax and MemorySwapMax=0; without the limit it exits.
// 2. spawn_bound(): starts a stand-in for rpc-server (default /usr/bin/sleep 600).
// 3. Console: "mem" prints the memory of the group; Ctrl+C, SIGTERM, hang-up
//    of the terminal or "quit" stop the child (SIGTERM, then SIGKILL) and exit.
//
// Records: DIR/before.json (first start, before re-exec), DIR/inside.json,
// DIR/child.json, DIR/ready.json, DIR/exit.json, DIR/child-exit.json, DIR/error.json.
#include <cstring>
#include <iostream>

#include <poll.h>
#include <sys/signalfd.h>
#include <termios.h>

#include "limits.hpp"
#include "report.hpp"

namespace {

using report::Json;
using swarm::limits::Memory;

std::string g_out = ".";

std::string describe(int status) {
    if (WIFEXITED(status)) return "exit " + std::to_string(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return std::string("signal ") + sigabbrev_np(WTERMSIG(status));
    return "status " + std::to_string(status);
}

std::string process_record(const Memory& memory) {
    char cwd[PATH_MAX] = "";
    if (getcwd(cwd, sizeof cwd) == nullptr) cwd[0] = '\0';
    const char* mark = std::getenv("DEMO_MARK");
    const bool tty = isatty(STDIN_FILENO) == 1;
    return Json()
        .num("pid", getpid())
        .num("starttime", report::starttime())
        .text("unit", memory.dir.substr(memory.dir.rfind('/') + 1))
        .flag("stdin_is_tty", tty)
        .flag("foreground", tty && tcgetpgrp(STDIN_FILENO) == getpgrp())
        .text("cwd", cwd)
        .text("demo_mark", mark != nullptr ? mark : "")
        .raw("memory", report::memory(memory))
        .str();
}

void print_memory(const Memory& m) {
    using swarm::limits::limit_text;
    std::cout << "memory.max=" << limit_text(m.max) << " memory.swap.max=" << limit_text(m.swap_max)
              << " current=" << m.current << " anon=" << m.anon() << " file=" << m.file()
              << " peak=" << (m.peak ? std::to_string(*m.peak) : "?") << " oom_kill=" << m.oom_kills() << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    uint64_t limit = uint64_t{8} << 30;  // MemoryMax=8G
    std::vector<std::string> child = {"/usr/bin/sleep", "600"};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            g_out = argv[++i];
        } else if (arg == "--limit" && i + 1 < argc) {
            limit = std::stoull(argv[++i]);
        } else if (arg == "--child" && i + 1 < argc) {
            child.assign(argv + i + 1, argv + argc);
            break;
        } else {
            std::cerr << "usage: swarm-demo [--out DIR] [--limit BYTES] [--child PATH [ARG...]]" << std::endl;
            return 2;
        }
    }
    if (std::getenv("SWARM_LIMIT_REEXEC") == nullptr)
        report::write_file(g_out + "/before.json",
                           process_record(swarm::limits::read_memory(swarm::limits::cgroup_dir())));

    Memory memory;
    try {
        memory = swarm::limits::enter_limited_scope(argv, limit, "swarm-demo-");
    } catch (const std::exception& error) {
        std::cerr << "Не удалось включить ограничение памяти: " << error.what()
                  << ". Без ограничения программа не запускается." << std::endl;
        report::write_file(g_out + "/error.json", Json().text("error", error.what()).str());
        return 2;
    }
    report::write_file(g_out + "/inside.json", process_record(memory));

    // Signals go to the event loop through signalfd. The mask is inherited by
    // fork and exec; spawn_bound() clears it in the child.
    sigset_t mask;
    sigemptyset(&mask);
    for (const int signal_number : {SIGINT, SIGTERM, SIGHUP, SIGCHLD}) sigaddset(&mask, signal_number);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    const int signals = signalfd(-1, &mask, SFD_CLOEXEC);

    const int log_fd = open((g_out + "/rpc.log").c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    pid_t rpc = -1;
    try {
        rpc = swarm::limits::spawn_bound(child, log_fd);
    } catch (const std::exception& error) {
        std::cerr << "Не удалось запустить " << child[0] << ": " << error.what() << std::endl;
        return 3;
    }
    const uint64_t oom_kills_at_start = memory.oom_kills();
    report::write_file(g_out + "/child.json",
                       Json()
                           .num("pid", rpc)
                           .num("starttime", report::starttime(std::to_string(rpc)))
                           .num("pgid", getpgid(rpc))
                           .text("cgroup", swarm::limits::cgroup_dir(std::to_string(rpc)))
                           .str());
    std::cout << "swarm-demo: группа " << memory.dir.substr(memory.dir.rfind('/') + 1) << ", memory.max="
              << memory.max << ", memory.swap.max=" << memory.swap_max << "; rpc-server pid " << rpc
              << ". Команды: mem, quit; Ctrl+C - выход." << std::endl;
    report::write_file(g_out + "/ready.json", Json().num("pid", getpid()).str());

    auto finish = [&](const std::string& reason, int code) {
        const std::string stopped = rpc > 0 ? describe(swarm::limits::stop_bound(rpc)) : "already exited";
        report::write_file(g_out + "/exit.json",
                           Json().text("reason", reason).text("child", stopped).num("code", code).str());
        std::cout << "Остановлено (" << reason << "), rpc-server: " << stopped << std::endl;
        return code;
    };

    bool stdin_open = true;
    std::string pending;
    while (true) {
        pollfd fds[2] = {{signals, POLLIN, 0}, {stdin_open ? STDIN_FILENO : -1, POLLIN, 0}};
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            return finish("poll failed", 1);
        }
        if ((fds[0].revents & POLLIN) != 0) {
            signalfd_siginfo info {};
            if (read(signals, &info, sizeof info) != static_cast<ssize_t>(sizeof info)) continue;
            const int signal_number = static_cast<int>(info.ssi_signo);
            if (signal_number != SIGCHLD) return finish(std::string("SIG") + sigabbrev_np(signal_number), 128 + signal_number);
            int status = 0;
            if (rpc > 0 && waitpid(rpc, &status, WNOHANG) == rpc) {
                const uint64_t oom_kills =
                    swarm::limits::read_memory(swarm::limits::cgroup_dir()).oom_kills() - oom_kills_at_start;
                std::cout << "rpc-server завершился: " << describe(status)
                          << (oom_kills > 0 ? ", его убило ядро из-за нехватки памяти" : "") << std::endl;
                report::write_file(g_out + "/child-exit.json",
                                   Json().text("status", describe(status)).num("oom_kill_delta", oom_kills).str());
                rpc = -1;
            }
            continue;
        }
        if ((fds[1].revents & (POLLIN | POLLHUP)) != 0) {
            char buffer[256];
            const ssize_t received = read(STDIN_FILENO, buffer, sizeof buffer);
            if (received <= 0) {
                stdin_open = false;
                continue;
            }
            pending.append(buffer, static_cast<size_t>(received));
            for (size_t end; (end = pending.find('\n')) != std::string::npos;) {
                std::string line = pending.substr(0, end);
                pending.erase(0, end + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line == "mem")
                    print_memory(swarm::limits::read_memory(swarm::limits::cgroup_dir()));
                else if (line == "quit")
                    return finish("quit", 0);
                else if (!line.empty())
                    std::cout << "Команды: mem, quit" << std::endl;
            }
        }
    }
}
