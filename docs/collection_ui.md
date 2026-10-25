# The Collection page on the shared collection

The launcher shows the same figures as the game and the Toy Box: SSARecomp's collection library (option A of
`figures_overlap.md`, now done). The screens are the Toy Box's organisation (`docs/toybox_ux.md`, screen 14) in
dorydev's look; her colours, fonts and widgets are untouched (`launcher_theme.h`).

## One engine

- `src/shared/collection/` and `src/shared/figures/` (all but `figure_icons.*`) are SSARecomp's files, copied by
  `tools/sync_collection.sh [--check] <ssa-recomp>` (same file list as SSARecomp's `tools/sync_skylauncher.sh`, into
  Skylanders' layout). Change them in SSARecomp, where the tests are, and copy them over. The catalogue
  (`resources/collection/catalogue.json`) is built into the executable (`cmake/embed_file.cmake`).
- `figures/figure_icons.*` is the launcher's: `IconStyle` (outline, fills, badge colours) draws the generated icons for any
  catalogue toy and variant kind; the library's own `collection/icons.*` is not used for drawing, only for
  `DiscImagePath` (the game's cached portraits are a second source after the launcher's own cache).
- Removed: `figure_file.*` and `figure_collection.*` (the old engine), replaced by `figure_tag`/`figure_crypto`/
  `figure_data` and `collection`.
- Folder: `collection::Folder()` (`%APPDATA%\Skylanders\Collection`, `~/Library/Application Support/Skylanders/Collection`).
  `SSA_COLLECTION` overrides it (tests, portable installs).

## Migration (never loses a figure)

At the first visit to the page (`CollectionPage::Open`):

1. `collection::Migrate(collection::LegacyFolder())` **copies** the old `<SSARecomp prefs>/figures` dumps into the
   collection; originals are never moved, changed or deleted, a second pass copies nothing. The game does the same at
   its start, so either side may go first.
2. The launcher's own earlier state moves once (`[collection] moved=1` in `launcher.ini` records it): favourites
   (`[collection] favourites`, file names) become `collection.json` favourites on the copies; the figures that
   started on the portal (`[portal] place`) become the next session's seats; `[portal] place` is then emptied, because
   `SSA_PORTAL_PLACE` overrides the session file at the game's start. The old keys are left in `launcher.ini` otherwise.
3. The never-played starters the game made on its own (37 of them, one per character) are not the player's: My
   Skylanders hides them (the header counts them), Manage > Tidy moves them to the trash.

## Seats (the next session)

- What the game reads at start is `portal.txt` in its save folder (`SSA_SAVE_DIR`, else
  `SDL_GetPrefPath("SSARecomp", "SSARecomp")`): `<slot> <path>` lines, rewritten by the game at every change. The launcher
  writes it with seat *i* in slot *i* (`WriteSeats`) and reads it back (`ReadSeats`): a figure goes to its slot's seat when
  it fits, else to the first seat that takes it (as the Toy Box does). `[portal] place` / `SSA_PORTAL_PLACE` are no longer
  written. The seat file is per game (`catalogue.json` `games[].seats`); only games with a runtime (SSA today) can be
  prepared, the others show their seats read-only.
- Drag a card onto a seat, or double-click it, Enter, A (it goes to the focused seat); every way of taking it off a seat (issue #9; a player, the magic item and the adventure pack alike, the others stay):
  the cross on the seat, Delete or the pad's X on the focused seat (or Delete with the mouse over it), the seat's right-click menu
  ("Take it off the seat", "Figure page") and dragging it off the seat onto the list; a seat whose figure is gone from the
  collection shows "Missing figure" and can be emptied the same way. The seats are read-only while the game runs.
  Under the seats one line sums the portal up (`collection::PortalOverviewLine`: "P1 Gill Grunt · Item Anvil Rain"); a figure
  held by a game, an unaccepted toy or a toy of a later game is refused with a message.
- Live: figures a game holds (JSON lock of a live process) show "LIVE", "ON PORTAL" and "On the portal in Spyro's
  Adventure"; the seats are read-only then and Edit/Rename-the-dump/Reset/Delete are disabled (metadata such as
  favourites stays editable). "It is not in use any more" (hold) breaks a stale lock.
- Refresh: every 1.5 s the model compares a signature of the folder (names, sizes, times of dumps, locks,
  `collection.json`, the session file) and whether each lock's process still runs; any change reloads the list, so a
  game that wrote a dump, took a figure or quit shows at once. Note the launcher window closes when the game starts
  (`main.cpp`), so this is seen when the launcher is opened beside a running game or after it.

## The screens

- **Header**: COLLECTION, counts, `+ Add a figure`, `Manage`, `View`.
- **Left**: "Portal for the next session", the game selector, one card per seat (Player 1, Player 2, Magic item,
  Adventure; Trap or Vehicle in later games).
- **Right (My Skylanders)**: a search box, a strip of the sections (click, LB/RB, Page Up/Down), the cards cut into
  sections by `collection::Group`: Smart is On the portal, Recent, Favourites, then by element; "Not for Spyro's
  Adventure" folded at the end. The list follows the focused seat (a seat shows what fits it).
- **View** (button, Y, F3): group by (Smart, Element, Game, Category, Series and variants, Level, Name), order, show
  (Everything, Favourites, Played, Never played), foreign toys folded or hidden; saved per game in `collection.json`
  (`views.<game>.launcher`).
- **Figure page** (click a card, right click, X, F2): stats from the dump, level bar, last played, where it plays; Put
  on a seat, Edit stats (level, XP, gold, hero points, nickname: refused for unreadable dumps), Rename (the label; the
  nickname is in Edit stats), Favourite, Duplicate, Back up now, Restore (list with level and gold, confirmation),
  Reset and Delete (hold 1 s; backed up first, Delete goes to the trash), Folder.
- **Add a figure**: a game selector (the six games), category chips, search; one card per character, "owned x2";
  the toys the game reads first, then "Later games" labelled "needs Giants". A side panel lists the versions
  (standard, variants, same-tag editions); name (optional) and Create; "Put it on Player 1" afterwards.
- **Manage**: Trash (put back, delete for good (hold); 30 days), Backups (per figure, restore), Import and export
  (files, a folder, drop files or a folder on the window; export everything; show in folder), Unknown toys (name one:
  `catalogue.json` of the collection, applied at the next start), Tidy (the starters).
- **Gamepad**: the same verbs as the Toy Box: d-pad moves (ImGui navigation), A chooses (puts on the seat), B goes
  back (closes the overlay, one step), X opens the Figure page, Y the View menu, LB/RB the previous/next section.

## Files

`collection_page.{h,cpp}` (page, seats, list, View), `collection_figure.cpp` (overlay, Figure page, Edit stats),
`collection_add.cpp`, `collection_manage.cpp` (Manage, test seed), `collection_model.{h,cpp}` (no UI: seats, signature
polling, settings move, naming unknown toys). `launcher_app.h` only holds a `CollectionPage` and makes it a friend;
`launcher_ui.cpp` calls `Draw`, `Drop`, `ClearIcons` and the test hooks.

## Checks

- `SkylauncherTest` (`CollectionChecks`): library, migration (originals intact, second pass copies none), stats,
  backups, restore, reset, duplicate, trash, import dedupe, JSON locks (live, stale), seats and `portal.txt`, live
  refresh, settings move, unknown toy naming, icons for every catalogue toy. Everything runs in a scratch folder
  (`SSA_COLLECTION`, `SSA_SAVE_DIR`, `SSA_MIGRATE_FROM` set by the test).
- Screenshots: `SSA_LAUNCHER_TEST_COLLECTION=1` with `SSA_LAUNCHER_SHOTS=<dir>` seeds a scratch `SSA_COLLECTION` (it
  refuses without it) and writes `20-collection` to `30-after-delete` (`SSA_LAUNCHER_TEST_FAKE_LOCKS=1` fakes a game's
  locks for the last one).
- End to end with the real game: seats prepared by the launcher, the game started with the same
  `SSA_COLLECTION`/`SSA_SAVE_DIR`, no `SSA_PORTAL_PLACE`: its log says which figures it took and the launcher shows the
  locks while it runs and the stats after.

## Not done

- Player 2 is a seat in the launcher; the game has no second pad yet.
- Names for figures are typed with the keyboard; no on-screen keyboard (the Toy Box has one for the TV).
- Real-portal copies ("Save a copy") are a Toy Box action; the launcher shows them as `origin: portal` figures.
- The launcher quits when the game starts, so live refresh is for a launcher opened next to a running game.
- The Toy Box's wheel pins (`GetWheel`) and pad glyph hints are not drawn in the launcher.

## Deleting a figure (issue #13)

What was wrong:
- The Delete button returned from the Figure page between `BeginDisabled` and `EndDisabled`. The disabled state leaked
  into the rest of the frame: the page went grey and the launcher stayed unusable.
- A hold button fired again every second while the mouse stayed down, so a refused action repeated its toast.
- `collection::Delete` moved the dump first and ignored the index write. When `collection.json` did not load (the S-03
  guard refuses writes), the file sat in `trash/` with no entry: it was lost to Manage > Trash.

Now: the index is written first and the move undone if it fails, so a refusal changes nothing and comes back as one
toast with its reason. A hold button acts once per press. After a delete the page returns to the list, the focus on the
next figure (`CollectionPage::DeleteFigure`, `CollectionModel::Delete`). Tested by `CollectionChecks` and screenshot
`30-after-delete`.
