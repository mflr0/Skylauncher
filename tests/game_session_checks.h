#pragma once

// Real child processes for lifecycle checks; deterministic window callbacks for headless PID routing checks.
inline void GameControlChecks(const std::filesystem::path& scratch)
{
    namespace fs = std::filesystem;
    const fs::path root = scratch / "game-control";
    fs::create_directories(root / "logs");
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SKY_LAUNCHER_LOG_DIR", (root / "logs").string().c_str(), true);
    fs::path runtime = fs::path(SDL_GetBasePath()) / "ArkeyanLauncherTest";
#if defined(_WIN32)
    runtime += ".exe";
#endif
    auto launch = [&](const char* name, const char* mode) {
        return GameLaunch{ runtime.string(), { { "SKY_FAKE_GAME_MODE", mode }, { "SKY_FAKE_GAME_READY", (root / name).string() } }, {} };
    };
    auto ready = [&](const char* name, GameSession& session) {
        uint32_t pid = 0;
        const uint64_t start = SDL_GetTicks();
        while (SDL_GetTicks() - start < 5000)
        {
            std::ifstream(root / name) >> pid;
            if (pid != 0) break;
            SDL_Delay(10);
        }
        SHELL_EXPECT(pid != 0 && pid == session.Pid());
    };
    auto waitEnd = [](GameSession& session) {
        for (uint64_t start = SDL_GetTicks(); SDL_GetTicks() - start < 8000; SDL_Delay(10))
            if (auto ended = session.Poll(SDL_GetTicks())) return ended;
        return std::optional<GameSession::Ended>();
    };
    bool available = true;
    uint32_t queried = 0, shown = 0;
    GameSession::WindowControl windows;
    windows.available = [&](uint32_t pid) { queried = pid; return available; };
    windows.show = [&](uint32_t pid) { shown = pid; return true; };
    GameSession session({ "ssa" }, [](const std::string&) { return false; }, windows);
    GameSession other({ "ssa" }, [](const std::string&) { return false; });
    std::string error;
    SHELL_EXPECT(!session.Stop(SDL_GetTicks()) && !session.CanShow() && !session.Show() && session.Pid() == 0);
    SHELL_EXPECT(session.Start("ssa", launch("first", "clean"), "--fake-game", error));
    ready("first", session);
    const uint32_t first = session.Pid();
    SHELL_EXPECT(session.Ours() && session.Running() == "ssa");
    // Another instance of the exact same executable must survive both clean and forced stops.
    SHELL_EXPECT(other.Start("ssa", launch("other", "clean"), "--fake-game", error));
    ready("other", other);
    SHELL_EXPECT(other.Pid() != first);
    SHELL_EXPECT(!other.CanShow() && !other.Show()); // the native backend has no visible window for this fake process
    SHELL_EXPECT(session.CanShow() && queried == first && session.Show() && shown == first);
    available = false;
    shown = 0;
    SHELL_EXPECT(!session.CanShow() && !session.Show() && shown == 0);
    available = true;
    SHELL_EXPECT(session.Stop(SDL_GetTicks()) && session.Stopping() && !session.CanShow() && !session.Show());
    SHELL_EXPECT(session.Stop(SDL_GetTicks()));
    auto ended = waitEnd(session);
    SHELL_EXPECT(ended && ended->stopped && ended->ours && ended->exitCode == 0 && ended->game == "ssa");
    SHELL_EXPECT(fs::exists(root / "first.closed"));
    SHELL_EXPECT(session.Running().empty() && !session.Ours() && !session.Stopping() && session.Pid() == 0);
    SHELL_EXPECT(!session.Stop(SDL_GetTicks()) && !session.Show());
    SHELL_EXPECT(!other.Poll(SDL_GetTicks()) && other.Ours() && !fs::exists(root / "other.closed"));

    SHELL_EXPECT(session.Start("ssa", launch("stubborn", "stubborn"), "--fake-game", error));
    ready("stubborn", session);
    const uint64_t stopAt = SDL_GetTicks();
    SHELL_EXPECT(session.Stop(stopAt));
    SDL_Delay(100);
    SHELL_EXPECT(!session.Poll(stopAt + GameSession::StopTimeoutMs - 1) && session.Ours());
    SHELL_EXPECT(!fs::exists(root / "stubborn.closed"));
    SHELL_EXPECT(session.Stop(stopAt + GameSession::StopTimeoutMs - 1));
    session.Poll(stopAt + GameSession::StopTimeoutMs); // the repeated press did not reset the deadline
    ended = waitEnd(session);
    SHELL_EXPECT(ended && ended->stopped && ended->exitCode != 0 && session.Running().empty());
    SHELL_EXPECT(!fs::exists(root / "stubborn.closed") && !other.Poll(SDL_GetTicks()) && other.Ours());
    SHELL_EXPECT(other.Stop(SDL_GetTicks()));
    ended = waitEnd(other);
    SHELL_EXPECT(ended && ended->stopped && ended->exitCode == 0 && fs::exists(root / "other.closed"));

    GameSession marked({ "ssa" }, [](const std::string&) { return true; }, windows);
    marked.Poll(SDL_GetTicks());
    queried = shown = 0;
    SHELL_EXPECT(marked.Running() == "ssa" && !marked.Ours() && marked.Pid() == 0);
    SHELL_EXPECT(!marked.Stop(SDL_GetTicks()) && !marked.Show() && !marked.CanShow() && queried == 0 && shown == 0);
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SKY_LAUNCHER_LOG_DIR");
    printf("Game controls: clean stop, timeout, same-executable PID isolation, window capability and activation routing checked\n");
}
