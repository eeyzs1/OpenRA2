#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct ScriptSource {
    std::string name, text;
    bool operator==(const ScriptSource&) const = default;
};

// Script-authored data, never executable bytecode. Installed source must match
// exactly before OnLoad is allowed to run in a staged, read-only VM.
struct ScriptSnapshot {
    bool active = false;
    uint64_t randomState = 0;
    std::vector<ScriptSource> sources;
    std::vector<uint8_t> state;
    bool write(FILE* file) const;
    bool read(FILE* file);
    bool operator==(const ScriptSnapshot&) const = default;
};
