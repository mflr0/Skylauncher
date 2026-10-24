#pragma once

// The install card's model: SSABuild's events (InstallProgress) turned into a fixed checklist of steps, one overall
// fraction and a time left. Pure: the UI draws it, SkylauncherTest checks it.
#include "launcher/game_install.h"
#include <cstdint>
#include <string>
#include <vector>

struct InstallChecklist
{
    enum class RowState { Waiting, Running, Done, Failed, Stopped };
    struct Row
    {
        std::string id, title;
        RowState state = RowState::Waiting;
        uint64_t ms = 0;        // how long it ran (0 when unknown: an install read back from its log)
        float fraction = -1;    // the running row's @progress, 0..1; -1 when it has none
        uint64_t done = 0, total = 0;
        bool timed = false;     // `ms` is known
    };

    std::vector<Row> rows;
    int current = -1;           // the running (or failed) row
    float overall = 0;          // 0..1, the rows weighted by their usual share of the time
    uint64_t elapsedMs = 0;     // since the first step (0 when unknown)
    int64_t etaMs = -1;         // time left; -1 until the current step has some progress to go by
    std::string action;         // what SSABuild is doing now, one short line
};

// The known steps, in SSABuild's order (tools/ssabuild/main.cpp in SSARecomp): "toolchain", "disc", "recompile",
// "shaders", "compile", "link", "movies", then "finish", which collects the second shaders/compile/link pass that
// follows the movie shaders. Other step ids get a row of their own before "finish".
InstallChecklist MakeInstallChecklist(const InstallProgress& progress, uint64_t now);

// What went wrong, for the player, in one sentence ("The compiler couldn't be downloaded..."); SSABuild's own message
// is shown under it.
std::string InstallFailureHeadline(const InstallChecklist& list, const InstallProgress& progress);

// "about 6 min left", "less than a minute left"; empty when unknown.
std::string InstallTimeLeft(int64_t ms);
// "3:07", "1:02:03".
std::string InstallDuration(uint64_t ms);
