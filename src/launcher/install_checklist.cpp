#include "launcher/install_checklist.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fmt/format.h>

namespace
{
    // Each step's usual share of a first install on a Windows laptop (the compile is most of it). Only the ratios matter.
    struct KnownStep
    {
        const char* id;
        const char* title;
        float weight;
    };
    constexpr KnownStep KnownSteps[] = {
        { "toolchain", "Preparing the compiler", 6 },
        { "disc", "Reading the disc", 2 },
        { "recompile", "Translating the game code", 10 },
        { "shaders", "Translating the shaders", 8 },
        { "compile", "Compiling the game", 50 },
        { "link", "Linking the game", 4 },
        { "movies", "Adding the movie shaders", 12 },
        { "finish", "Finishing", 8 },
    };
    constexpr float UnknownWeight = 4;
}

InstallChecklist MakeInstallChecklist(const InstallProgress& progress, uint64_t now)
{
    InstallChecklist list;
    std::vector<float> weights;
    for (const KnownStep& step : KnownSteps)
    {
        list.rows.push_back({ step.id, step.title });
        weights.push_back(step.weight);
    }
    auto rowOf = [&](const std::string& id) -> int {
        for (size_t i = 0; i < list.rows.size(); i++)
            if (list.rows[i].id == id)
                return int(i);
        return -1;
    };

    // Each @step lands on its row; after "movies", the shaders/compile/link pass again: that is "finish".
    bool afterMovies = false;
    int last = -1, lastStepRow = -1;
    for (const InstallProgress::Step& step : progress.steps)
    {
        int row = afterMovies && (step.id == "shaders" || step.id == "compile" || step.id == "link") ? rowOf("finish") : rowOf(step.id);
        if (step.id == "movies")
            afterMovies = true;
        if (row < 0)
        {
            row = int(list.rows.size()) - 1; // before "finish"
            list.rows.insert(list.rows.begin() + row, { step.id, step.title });
            weights.insert(weights.begin() + row, UnknownWeight);
        }
        InstallChecklist::Row& r = list.rows[size_t(row)];
        // A step seen again (the finish pass's several steps) adds up its time.
        bool timed = step.startedAt != 0 && (step.endedAt != 0 || progress.state == InstallProgress::State::Running);
        uint64_t end = step.endedAt != 0 ? step.endedAt : now;
        r.ms += timed && end > step.startedAt ? end - step.startedAt : 0;
        r.timed = r.timed || timed;
        r.state = InstallChecklist::RowState::Done;
        // Rows skipped between two steps (an older kit without one of them) count as done.
        for (int i = last + 1; i < row; i++)
            if (list.rows[size_t(i)].state == InstallChecklist::RowState::Waiting)
                list.rows[size_t(i)].state = InstallChecklist::RowState::Done;
        last = std::max(last, row);
        lastStepRow = row;
    }

    // The last step is the one running, or the one that failed or was stopped.
    using State = InstallProgress::State;
    if (!progress.steps.empty() && progress.state != State::Done)
    {
        int row = lastStepRow;
        list.current = row;
        InstallChecklist::Row& r = list.rows[size_t(row)];
        r.state = progress.state == State::Running ? InstallChecklist::RowState::Running
            : progress.state == State::Failed ? InstallChecklist::RowState::Failed
                                              : InstallChecklist::RowState::Stopped;
        if (progress.total > 0)
        {
            r.done = std::min(progress.done, progress.total);
            r.total = progress.total;
            r.fraction = float(double(r.done) / double(r.total));
        }
        for (size_t i = size_t(row) + 1; i < list.rows.size(); i++)
            list.rows[i].state = InstallChecklist::RowState::Waiting;
    }
    else if (progress.state == State::Done)
        for (InstallChecklist::Row& r : list.rows)
            r.state = InstallChecklist::RowState::Done;
    else if (progress.state == State::Running)
        list.current = 0; // SSABuild has started but said nothing yet

    // The overall fraction, by weight.
    float total = 0, done = 0;
    for (size_t i = 0; i < list.rows.size(); i++)
    {
        total += weights[i];
        if (list.rows[i].state == InstallChecklist::RowState::Done)
            done += weights[i];
        else if (int(i) == list.current && list.rows[i].fraction > 0)
            done += weights[i] * list.rows[i].fraction;
    }
    list.overall = progress.state == State::Done ? 1.0f : total > 0 ? std::clamp(done / total, 0.0f, 1.0f) : 0.0f;

    if (!progress.steps.empty() && progress.steps.front().startedAt != 0)
    {
        uint64_t end = progress.state == State::Running ? now : std::max(progress.steps.back().endedAt, progress.steps.back().startedAt);
        list.elapsedMs = end > progress.steps.front().startedAt ? end - progress.steps.front().startedAt : 0;
    }

    // Time left, once the current step has progress: its own pace for the rest of it, and for the steps still to come
    // the pace of the steps that took real time (an "up to date" step takes none and says nothing about the machine).
    if (progress.state == State::Running && list.current >= 0)
    {
        const InstallChecklist::Row& r = list.rows[size_t(list.current)];
        float fraction = r.fraction;
        if (fraction >= 0.02f && r.ms >= 2000)
        {
            float currentLeft = float(r.ms) / fraction * (1 - fraction);
            double measuredMs = double(r.ms), measuredWeight = double(weights[size_t(list.current)]) * fraction;
            for (size_t i = 0; i < size_t(list.current); i++)
                if (list.rows[i].state == InstallChecklist::RowState::Done && list.rows[i].timed && list.rows[i].ms >= 3000)
                {
                    measuredMs += double(list.rows[i].ms);
                    measuredWeight += weights[i];
                }
            double perWeight = measuredMs / measuredWeight, rest = 0;
            for (size_t i = size_t(list.current) + 1; i < list.rows.size(); i++)
                rest += weights[i] * perWeight;
            list.etaMs = int64_t(currentLeft + rest);
        }
    }

    // The current action: SSABuild's last @log line in the step, else its count.
    if (progress.state == State::Running)
    {
        list.action = progress.action;
        if (list.action.empty() && progress.total > 0)
            list.action = fmt::format("{} of {}", progress.done, progress.total);
        if (list.action.empty())
            list.action = progress.steps.empty() ? "Starting the installer" : "Working";
    }
    return list;
}

std::string InstallTimeLeft(int64_t ms)
{
    if (ms < 0)
        return {};
    if (ms < 60000)
        return "less than a minute left";
    int64_t minutes = (ms + 30000) / 60000;
    if (minutes < 60)
        return fmt::format("about {} min left", minutes);
    return fmt::format("about {} h {:02} min left", minutes / 60, minutes % 60);
}

std::string InstallDuration(uint64_t ms)
{
    uint64_t seconds = ms / 1000;
    if (seconds >= 3600)
        return fmt::format("{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60);
    return fmt::format("{}:{:02}", seconds / 60, seconds % 60);
}

std::string InstallFailureHeadline(const InstallChecklist& list, const InstallProgress& progress)
{
    auto has = [&](const char* text) {
        std::string lower = progress.error;
        for (char& c : lower)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return lower.find(text) != std::string::npos;
    };
    if (has("kit wasn't found") || has("couldn't start"))
        return "The installer couldn't start.";
    if (has("space") || has("no room") || has("disk full"))
        return "There isn't enough free space on the drive (about 6 GB are needed).";
    if (has("cannot write") || has("couldn't create") || has("permission") || has("access is denied"))
        return "A file of the game folder couldn't be written.";
    std::string step = list.current >= 0 ? list.rows[size_t(list.current)].id : std::string();
    if (step == "toolchain" && (has("download") || has("curl") || has("http") || has("sha-256") || has("checksum")))
        return "The compiler couldn't be downloaded. Check the internet connection.";
    if (step == "disc")
        return "The disc image couldn't be read. Check that the file is complete.";
    if (list.current >= 0)
    {
        std::string title = list.rows[size_t(list.current)].title;
        if (!title.empty())
            title[0] = char(std::tolower(static_cast<unsigned char>(title[0])));
        return "Something went wrong while " + title + ".";
    }
    return "The installer stopped before it could build the game.";
}
