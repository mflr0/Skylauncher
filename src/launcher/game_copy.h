#pragma once

// "Copy the game data" as the launcher uses it (docs/launcher_ui.md "Game data", docs/install.md): which source a game plays
// from, and the copy job that runs on a worker thread. The files, the manifest and the checks are shared/collection/game_data.h.
// No UI: SkylauncherTest runs these on scratch folders.
#include "launcher/launcher.h"
#include "shared/collection/game_data.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

// Where a game's files come from: its complete copy when there is one, else the disc image the player chose. Nothing is
// read from the disc here; the caller still verifies the path (CheckGameSource accepts both).
struct GameSourcePick
{
    std::string path;         // the copy's folder, or `iso` (empty when there is neither)
    bool copied = false;      // `path` is the copy
    gamedata::Status copy;    // what the copy is: Damaged and Partial are fallbacks to the disc image, said to the player
};
GameSourcePick PickGameSource(const GameInfo& game, const std::string& iso);

// The copy running in the background. Start reads `image` (a disc image already verified), writes <data>/<Game>/gamedata.
class GameCopyJob
{
public:
    ~GameCopyJob() { Cancel(); Join(); }
    // Started and not yet collected (it may have finished: Done).
    bool Running() const { return m_started; }
    bool Done() const { return m_done; }
    void Start(const GameInfo& game, const std::string& image, const std::string& xexSha256, bool cancelAtOnce = false /* tests */);
    // Stops between two chunks; the files copied so far stay and a later Start resumes.
    void Cancel() { m_cancel = true; }
    float Fraction() const;
    uint64_t Bytes() const { return m_bytes; }
    uint64_t Total() const { return m_total; }
    // Waits for the end and returns how it went; the job can start again.
    gamedata::CopyResult Collect();

private:
    void Join() { if (m_thread.joinable()) m_thread.join(); }

    std::thread m_thread;
    std::atomic<bool> m_started{ false }, m_done{ false }, m_cancel{ false };
    std::atomic<uint64_t> m_bytes{ 0 }, m_total{ 0 };
    gamedata::CopyResult m_result;
};

// What to tell the player when a copy didn't finish ("Not enough space: ..."); empty for Done and Cancelled.
std::string CopyFailureText(const gamedata::CopyResult& result);
