// SkylauncherTest's checks for the launcher shell: the install checklist, game runs and logs, the updater, settings.
// Called from launcher_test.cpp's main; returns the number of failures.
#include "launcher/game_install.h"
#include "launcher/install_checklist.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    int g_shellFailures = 0;

#define SHELL_EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_shellFailures++; } } while (0)

    InstallProgress Feed(std::initializer_list<std::pair<const char*, uint64_t>> lines)
    {
        InstallProgress progress;
        progress.state = InstallProgress::State::Running;
        for (const auto& [line, at] : lines)
            progress.Apply(line, at);
        return progress;
    }

    void ChecklistChecks(const std::filesystem::path& scratch)
    {
        using RowState = InstallChecklist::RowState;
        // Nothing yet: eight waiting rows, the first one current.
        InstallChecklist empty = MakeInstallChecklist(Feed({}), 1000);
        SHELL_EXPECT(empty.rows.size() == 8 && empty.current == 0 && empty.overall == 0 && empty.etaMs == -1);
        SHELL_EXPECT(empty.rows.front().title == "Preparing the compiler" && empty.rows.back().id == "finish");
        SHELL_EXPECT(empty.action == "Starting the installer");

        // Two steps done, the third running at a quarter: ticks with times, a spinner, a bar, a time left.
        InstallProgress progress = Feed({ { "@step toolchain Preparing the compiler", 1000 }, { "@log downloading llvm-mingw", 1500 },
            { "@step disc Reading the disc", 61000 }, { "@step recompile Translating the game code", 91000 }, { "@progress 10 40", 100000 },
            { "@log ppc_recomp.12.cpp", 120000 } });
        InstallChecklist list = MakeInstallChecklist(progress, 121000);
        SHELL_EXPECT(list.current == 2 && list.rows[2].state == RowState::Running && list.rows[2].fraction == 0.25f);
        SHELL_EXPECT(list.rows[0].state == RowState::Done && list.rows[0].ms == 60000 && list.rows[1].ms == 30000);
        SHELL_EXPECT(list.rows[3].state == RowState::Waiting && list.rows[7].state == RowState::Waiting);
        SHELL_EXPECT(list.elapsedMs == 120000 && list.action == "ppc_recomp.12.cpp");
        SHELL_EXPECT(list.overall > 0.08f && list.overall < 0.12f); // (6 + 2 + 10/4) of 100
        // The current step: 30 s for a quarter, 90 s left of it; the rest at the measured pace (90 s for 6+2+2.5 weight).
        SHELL_EXPECT(list.etaMs > 90000 + 800000 && list.etaMs < 90000 + 1000000);
        SHELL_EXPECT(InstallTimeLeft(list.etaMs).starts_with("about ") && InstallTimeLeft(30000) == "less than a minute left");
        SHELL_EXPECT(InstallTimeLeft(-1).empty() && InstallTimeLeft(3 * 3600000 + 5 * 60000) == "about 3 h 05 min left");
        SHELL_EXPECT(InstallDuration(187000) == "3:07" && InstallDuration(3723000) == "1:02:03");

        // A new step: its bar starts again, the time left waits for its progress.
        progress.Apply("@step shaders Translating the shaders", 130000);
        list = MakeInstallChecklist(progress, 131000);
        SHELL_EXPECT(list.current == 3 && list.rows[3].fraction < 0 && list.etaMs == -1 && list.action == "Working");

        // After the movie shaders, the second pass is "Finishing", whose time adds up.
        for (const char* line : { "@step compile Compiling the game", "@step link Linking the game", "@step movies Adding the movie shaders",
                 "@step shaders Translating the shaders", "@step compile Compiling the game" })
            progress.Apply(line, 200000);
        progress.Apply("@progress 1 4", 201000);
        list = MakeInstallChecklist(progress, 205000);
        SHELL_EXPECT(list.current == 7 && list.rows[7].state == RowState::Running && list.rows[6].state == RowState::Done);
        SHELL_EXPECT(list.rows[3].state == RowState::Done && list.rows[3].ms == 70000);
        progress.Apply("@done /games/x/SSARecomp", 206000);
        progress.state = InstallProgress::State::Done;
        list = MakeInstallChecklist(progress, 206000);
        SHELL_EXPECT(list.overall == 1.0f && list.current == -1);
        for (const InstallChecklist::Row& row : list.rows)
            SHELL_EXPECT(row.state == RowState::Done);

        // A failure: the row is marked, the message is for people.
        InstallProgress failed = Feed({ { "@step toolchain Preparing the compiler", 1000 }, { "@error download failed: curl exit 6", 2000 } });
        failed.state = InstallProgress::State::Failed;
        list = MakeInstallChecklist(failed, 3000);
        SHELL_EXPECT(list.rows[0].state == RowState::Failed && list.current == 0);
        SHELL_EXPECT(InstallFailureHeadline(list, failed).find("couldn't be downloaded") != std::string::npos);
        failed = Feed({ { "@step compile Compiling the game", 1000 }, { "@error 3 files didn't compile", 2000 } });
        failed.state = InstallProgress::State::Failed;
        list = MakeInstallChecklist(failed, 3000);
        SHELL_EXPECT(list.rows[0].state == RowState::Done && list.rows[4].state == RowState::Failed);
        SHELL_EXPECT(InstallFailureHeadline(list, failed) == "Something went wrong while compiling the game.");

        // An unknown step gets its own row, before "Finishing".
        list = MakeInstallChecklist(Feed({ { "@step toolchain Preparing the compiler", 1 }, { "@step pack Packing the textures", 2 } }), 3);
        SHELL_EXPECT(list.rows.size() == 9 && list.rows[7].id == "pack" && list.rows[7].title == "Packing the textures" && list.current == 7);

        // Read back from install.log after a restart: the steps without their times, Interrupted.
        std::filesystem::create_directories(scratch / "interrupted");
        std::ofstream(scratch / "interrupted" / "install.log") << "@step toolchain Preparing the compiler\n@log x\n"
                                                                     "@step disc Reading the disc\n@progress 2 4\n";
        InstallProgress past = ReadInstallLog(scratch / "interrupted");
        SHELL_EXPECT(past.state == InstallProgress::State::Interrupted && past.steps.size() == 2);
        list = MakeInstallChecklist(past, 99999);
        SHELL_EXPECT(list.current == 1 && list.rows[1].state == RowState::Stopped && list.rows[0].state == RowState::Done);
        SHELL_EXPECT(!list.rows[0].timed && list.elapsedMs == 0 && list.etaMs == -1);
        std::ofstream(scratch / "interrupted" / "install.log", std::ios::app) << "@error disc image unreadable\n";
        SHELL_EXPECT(ReadInstallLog(scratch / "interrupted").state == InstallProgress::State::Failed);
        SHELL_EXPECT(ReadInstallLog(scratch / "nowhere").state == InstallProgress::State::Idle);
        printf("Install checklist: steps, finish pass, times, failures and read-back checked\n");
    }
}

int ShellChecks(const std::filesystem::path& scratch)
{
    ChecklistChecks(scratch);
    return g_shellFailures;
}
