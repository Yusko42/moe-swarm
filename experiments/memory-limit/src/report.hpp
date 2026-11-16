// report.hpp - small JSON records of the test programs (not part of the recipe).
#pragma once

#include <cstdio>
#include <optional>
#include <string>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "limits.hpp"

namespace report {

class Json {
public:
    Json& text(const std::string& key, const std::string& value) { return put(key, quote(value)); }
    template <typename T>
    Json& num(const std::string& key, T value) { return put(key, std::to_string(value)); }
    Json& flag(const std::string& key, bool value) { return put(key, value ? "true" : "false"); }
    Json& raw(const std::string& key, const std::string& json) { return put(key, json); }
    Json& opt(const std::string& key, const std::optional<uint64_t>& value) {
        return put(key, value ? std::to_string(*value) : "null");
    }
    Json& limit(const std::string& key, uint64_t value) {
        return value == swarm::limits::kNoLimit ? text(key, "max") : num(key, value);
    }
    std::string str() const { return "{" + body_ + "}"; }

    static std::string quote(const std::string& value) {
        std::string out = "\"";
        for (const char c : value) {
            if (c == '"' || c == '\\') out += '\\';
            if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                out += escaped;
                continue;
            }
            out += c;
        }
        return out + "\"";
    }

private:
    Json& put(const std::string& key, const std::string& value) {
        if (!body_.empty()) body_ += ", ";
        body_ += quote(key) + ": " + value;
        return *this;
    }
    std::string body_;
};

inline std::string pairs(const std::map<std::string, uint64_t>& values) {
    Json json;
    for (const auto& [key, value] : values) json.num(key, value);
    return json.str();
}

inline std::string memory(const swarm::limits::Memory& m) {
    return Json()
        .text("dir", m.dir)
        .limit("max", m.max)
        .limit("swap_max", m.swap_max)
        .num("current", m.current)
        .opt("peak", m.peak)
        .opt("swap_current", m.swap_current)
        .opt("swap_peak", m.swap_peak)
        .num("anon", m.anon())
        .num("file", m.file())
        .raw("events", pairs(m.events))
        .str();
}

// Field 22 of /proc/PID/stat: start time in clock ticks. With the PID it
// identifies a process even if the number is later reused.
inline uint64_t starttime(const std::string& pid = "self") {
    const std::string stat = swarm::limits::read_text("/proc/" + pid + "/stat").value_or("");
    const auto close = stat.rfind(')');
    if (close == std::string::npos) return 0;
    std::istringstream fields(stat.substr(close + 2));  // field 3 onward
    std::string field;
    for (int index = 3; index <= 22 && (fields >> field); ++index) {
    }
    try {
        return std::stoull(field);
    } catch (const std::exception&) {
        return 0;
    }
}

inline std::string identity(const std::string& role) {
    int pdeathsig = 0;
    prctl(PR_GET_PDEATHSIG, &pdeathsig);
    return Json()
        .text("role", role)
        .num("pid", getpid())
        .num("ppid", getppid())
        .num("pgid", getpgrp())
        .num("sid", getsid(0))
        .num("starttime", starttime())
        .num("pdeathsig", pdeathsig)
        .raw("memory", memory(swarm::limits::read_memory(swarm::limits::cgroup_dir())))
        .str();
}

inline void write_file(const std::string& path, const std::string& content) {
    const std::string temporary = path + ".tmp" + std::to_string(getpid());
    if (FILE* file = std::fopen(temporary.c_str(), "w")) {
        std::fputs(content.c_str(), file);
        std::fputc('\n', file);
        std::fclose(file);
        std::rename(temporary.c_str(), path.c_str());
    }
}

inline bool exists(const std::string& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
}

inline std::string self_exe() {
    char path[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", path, sizeof path - 1);
    if (length <= 0) return {};
    path[length] = '\0';
    return path;
}

}  // namespace report
