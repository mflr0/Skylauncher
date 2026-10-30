#pragma once

// Run the real page against the test executable's fake process mode, with every writable folder in scratch.
static void LauncherControlChecks(const std::filesystem::path& scratch)
{
    namespace fs = std::filesystem;
    const fs::path root = scratch / "controls";
    fs::create_directories(root / "home");
    fs::create_directories(root / "shots");
    fs::path fake = fs::path(SDL_GetBasePath()) / "ArkeyanLauncherTest";
#if defined(_WIN32)
    fake += ".exe";
#endif
    SDL_Environment* environment = CreateGameEnvironment({
        { "HOME", (root / "home").string() }, { "CFFIXED_USER_HOME", (root / "home").string() },
        { "SDL_VIDEO_DRIVER", "offscreen" }, { "SDL_AUDIO_DRIVER", "dummy" },
        { "SKYLANDERS_DATA", (root / "data").string() }, { "SKY_LAUNCHER_DATA", (root / "launcher").string() },
        { "SKY_COLLECTION", (root / "collection").string() }, { "SKY_SAVE_DIR", (root / "saves").string() },
        { "SKY_LAUNCHER_INI", (root / "launcher.ini").string() }, { "SKY_SETTINGS", "" },
        { "SKY_LAUNCHER_LOG_DIR", (root / "logs").string() }, { "SKY_LAUNCHER_NO_UPDATE_CHECK", "1" },
        { "SKY_LAUNCHER_RUNTIME", fake.string() }, { "SKY_LAUNCHER_SHOTS", (root / "shots").string() },
        { "SKY_LAUNCHER_TEST_RUNNING", "controls" }, { "SKY_LAUNCHER_TEST_DROP", "" },
        { "SKY_FAKE_GAME_MODE", "slow-close" }, { "SKY_FAKE_GAME_READY", (root / "ready").string() }
    });
    SDL_IOStream* log = SDL_IOFromFile((root / "output.log").string().c_str(), "wb");
    const char* arguments[] = { SKYLAUNCHER_TEST_EXE, nullptr };
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments);
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, log);
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    SDL_DestroyEnvironment(environment);
    EXPECT(process != nullptr);
    int exitCode = -1;
    bool ended = false;
    if (process != nullptr)
    {
        for (uint64_t start = SDL_GetTicks(); SDL_GetTicks() - start < 30000 && !ended; SDL_Delay(20))
            ended = SDL_WaitProcess(process, false, &exitCode);
        if (!ended) { SDL_KillProcess(process, true); SDL_WaitProcess(process, true, &exitCode); }
        SDL_DestroyProcess(process);
    }
    if (log != nullptr) SDL_CloseIO(log);
    EXPECT(ended && exitCode == 0);
    const std::string output = ReadLogTail(root / "output.log");
    if (output.find("FAIL") != std::string::npos) fprintf(stderr, "%s\n", output.c_str());
    EXPECT(output.find("FAIL") == std::string::npos);
    EXPECT(fs::exists(root / "ready.closed"));
    for (const char* name : { "started", "stopping", "ready" })
        EXPECT(fs::exists(root / "shots" / (std::string("running-control-") + name + ".png")));
    printf("Game page: session timer advances, STOP disables while stopping, unsupported Show hidden, no running pickers or gear, ready after stopping\n");
}
