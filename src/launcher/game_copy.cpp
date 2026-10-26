#include "launcher/game_copy.h"
#include "launcher/ui/launcher_ui.h"
#include "shared/collection/datadir.h"
#include "shared/vfs.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <fmt/format.h>



GameSourcePick PickGameSource(const GameInfo& game, const std::string& iso)
{
    GameSourcePick pick;
    pick.copy = gamedata::Inspect(gamedata::Folder(game.id));
    if (pick.copy.state == gamedata::State::Complete)
    {
        pick.path = datadir::Text(gamedata::Folder(game.id));
        pick.copied = true;
    }
    else
        pick.path = iso;
    return pick;
}

float GameCopyJob::Fraction() const
{
    uint64_t total = m_total;
    return total == 0 ? 0.0f : float(double(m_bytes.load()) / double(total));
}

void GameCopyJob::Start(const GameInfo& game, const std::string& image, const std::string& xexSha256, bool cancelAtOnce)
{
    Join();
    m_started = true;
    m_done = false;
    m_cancel = cancelAtOnce;
    m_bytes = m_total = 0;
    m_result = {};
    m_thread = std::thread([this, id = std::string(game.id), image, xexSha256] {
        DiscFileSystem disc;
        if (!disc.Open(image.c_str()))
        {
            m_result.error = "couldn't open the disc image";
            m_done = true;
            return;
        }
        gamedata::Source source;
        source.xexSha256 = xexSha256;
        for (const auto& [path, entry] : disc.Files())
            source.files.push_back({ path, entry->size });
        source.read = [&disc](const gamedata::SourceFile& file, uint64_t position, void* buffer, size_t size) -> size_t {
            const DiscFileSystem::Entry* entry = disc.Find(file.path);
            return entry == nullptr ? 0 : disc.Read(*entry, position, buffer, uint32_t(size));
        };
        gamedata::CopyOptions options;
        options.cancel = &m_cancel;
        options.progress = [this](uint64_t done, uint64_t total) { m_total = total; m_bytes = done; };
        if (const char* free = SDL_getenv("SKYLAUNCHER_COPY_FREE_BYTES")) // tests: the room to assume
            options.freeBytes = std::strtoll(free, nullptr, 10);
        m_result = gamedata::Copy(source, gamedata::Folder(id), options);
        m_done = true;
    });
}

gamedata::CopyResult GameCopyJob::Collect()
{
    Join();
    gamedata::CopyResult result = m_result;
    m_started = m_done = false;
    return result;
}

std::string CopyFailureText(const gamedata::CopyResult& result)
{
    using Outcome = gamedata::CopyResult::Outcome;
    if (result.outcome == Outcome::NotEnoughSpace)
        return fmt::format("Not enough space: the copy needs {} and this drive has {} free.", LauncherFormatSize(result.needed),
            LauncherFormatSize(result.available));
    if (result.outcome == Outcome::Failed)
        return "The copy stopped: " + result.error + ". Press the button again to carry on from there.";
    return {};
}
