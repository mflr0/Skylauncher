# Figures: Skylauncher's engine and SSARecomp's collection library

Decided: option A, done (`docs/collection_ui.md`). What follows is the comparison as it stood before. Skylauncher's figure engine (`src/shared/figures/`, used by the Collection
page) and SSARecomp's collection library (`docs/collection.md` and `docs/collection_api.md` in that repo; code in
`runtime/host/figures/` and `runtime/host/collection/` (`SSARecomp/host/` in older checkouts), with `tools/sync_skylauncher.sh` to copy it here) do the
same jobs.

| Job | Skylauncher (`src/shared/figures/`) | SSARecomp library |
| --- | --- | --- |
| Tag dump: header, CRC, AES, stats, nickname, reset | `figure_file.*` | `figures/figure_data.*`, `figure_crypto.*`, `figure_tag.*` |
| Toy catalogue | `figure_catalog.*`: a C++ table, Spyro's Adventure's toys only | `collection/catalogue.*` + `catalogue.json`: six games, variants, which game accepts what |
| Folder: import, new figure, backups, restore, delete | `figure_collection.*` | `collection/collection.*` (also rename, labels, editions, migration) |
| Icons when the disc has no picture | `figure_icons.*` (`IconStyle`) | `collection/icons.*` (shape per category, variant badges) |
| Disc portraits | one shared provider since 0.4 (`src/shared/art/`): both cache them in `<data>/<Game>/portraits/` | the same (`runtime/host/art/`) |

## Where they disagree (what a player would notice)

1. **Folder.** Skylauncher reads `<SSARecomp prefs>/figures` (`App::FiguresFolder`). The game now uses
   `<Skylanders data folder>/Collection` (`%APPDATA%\Skylanders\Collection` on Windows) and only *copies* the old
   folder into it at start. So a figure made in the launcher reaches the game once, and what the game writes
   (experience, gold) never shows in the launcher.
2. **Locks.** Skylauncher writes and reads `<dump>.lock` as a bare process id; the game now writes JSON
   (`{ "game", "pid", "since" }`). Skylauncher's `IsLocked` reads no number from it and treats the figure as free,
   so it could rewrite a figure the game holds.
3. **Favourites.** Skylauncher: `[collection] favourites=` in `launcher.ini`. Library: `collection.json`.
4. **Backups.** Both use `backups/<...>/<date>.sky`, but the folder is the file's stem here
   (`backups/014 Gill Grunt/`) and the full file name there (`backups/014 Gill Grunt.sky/`), with different date formats.
5. **Catalogue.** Skylauncher knows Spyro's Adventure's toys only; the library knows six games and the variants.
6. **`[portal] place`.** Both use file names; the library also takes paths relative to the collection.

## Options

- **A. Adopt the library** as `docs/collection_api.md` describes: sync the files, point the Collection page at
  `collection::Folder()`, call `collection::Migrate(collection::LegacyFolder())` once, and keep dorydev's page and
  look on top of the library's calls. Removes points 1 to 6; the page code changes, the screens need not.
- **B. Keep Skylauncher's engine** and align it: the folder, the JSON lock, `collection.json`. Two codebases to keep
  in step.

Points 1 and 2 matter as soon as a player uses both the launcher's Collection page and the game.
