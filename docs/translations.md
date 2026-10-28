# Translations

The launcher's own text (pages, buttons, states, notifications, dialogs, install steps, errors, tooltips) is in the player's
language. The game's text and voice languages are a different thing: they are the game's, set in each game's settings
(Language tab, `[<game>] language`).

## How it works
- **English is the source, and the key.** The code writes the English text and asks for it translated: `Tr("Cancel")`.
  `Tf("{} min ago", minutes)` formats too. A string that sits in a table or a constant (where it can't call `Tr` yet) is
  marked `N_("...")` and goes through `Tr` where it is shown. Both are in `src/launcher/i18n.h`.
- **One file per language:** `resources/lang/<code>.txt`, built into the executable by CMake (no code change to add one). A string
  a language doesn't have shows in English, so a language can be added before it is complete.
- **The file format:** `@code fr` and `@name Français` (the language's own name, shown in the menu), `#` comments, then one
  `English<TAB>translation` per line; `\n` is a line break. The English is the text of the code, character for character.
- **Placeholders:** `{}` is replaced by the next argument; `{0}`, `{1}`... pick one by its number, so a language can put them in
  another order (`{1} of {0}`). The format part is kept (`{:08X}`, `{:g}`). A translation that doesn't fit the arguments is
  replaced by the English one at run time, and fails the test.
- **A text a language says in two ways** (singular and plural) is two English strings (`Copied: {}, 1 file`, `Copied: {}, {} files`);
  a sentence is never built from pieces.
- **Not translated:** names (Arkeyan Launcher, the games, the figures), the text of the problem report (it goes to the maintainers),
  the command-line help, and what other programs print: the installer kit's own lines in the install log, the game's portal
  test answers, GitHub's messages. A message built before the language changed (a toast already shown, a disc check's result)
  stays in the language it was made in until it is made again.

## Which language is shown
The launcher's language is its own: it never follows the game that is shown. Settings > Launcher > Language:
**System default**, or one language of the list. Remembered in `[launcher] language` (`docs/settings.md`) and applied at once,
with no restart.
- **System default** (the default, and an empty setting): the system's language, the first of the system's preferred languages
  the launcher has, else English.
- **A language** is that language; a code that has no file in `resources/lang` shows English.
- An old `game` (follow the shown game's text language) is read as `System default` and erased at the next save.
- `SKY_LAUNCHER_LANGUAGE=<code>` forces a language (the tests and the screenshots use it).

## Adding a language
1. Take the language's code from the disc's: `de`, `es`, `it`, `nl`, `sv`, `no`, `da`, `fi` (the codes of
   `LanguageChoices` in `ui/launcher_app.h`; Norwegian is `no`, whatever the system calls it).
2. Make the list of strings: `SKY_LAUNCHER_TEST_I18N_DUMP=/path/to/list.txt SKY_LAUNCHER_TEST_I18N_ONLY=1 ArkeyanLauncherTest x <scratch>`
   writes every English string, each followed by a tab, in the file format. Copy it to `resources/lang/<code>.txt`, add
   `@code` and `@name` at the top, and write the translations after the tabs.
3. Build (CMake finds the file). The language is in the menu.
4. Run `SKY_LAUNCHER_TEST_I18N_ALL=1 SKY_LAUNCHER_TEST_I18N_ONLY=1 ArkeyanLauncherTest x <scratch>`: it lists the strings still
   missing, the placeholders that differ from the English, and the entries the code no longer has. The key coverage test is part
   of the full `ArkeyanLauncherTest`.
5. Look at it in the window, smallest size included (`SKY_LAUNCHER_LANGUAGE=<code>`; `SKY_LAUNCHER_TEST_SIZE=1024x680` with
   `SKY_LAUNCHER_SHOTS` takes the screenshot test's pictures at that size): a language is often longer than English. Buttons
   widen to their label; a long sentence is better shortened.

## For a developer
- Every user-visible string goes through `Tr`, `Tf` or `N_`. The test (`tests/i18n_test.cpp`) reads the sources, collects every
  such string and checks each language file against them, and refuses a plain literal given straight to a button, a title, a tooltip
  or a text call. Changing an English string is changing its key: update the language files (the test lists what no longer matches).
- The components' labels count as button text for that check (`ui::Button`, `ui::Pill`, `ui::Chip`, `ui::Badge`,
  `ui::StatusPill`, `ui::Toggle`, `ui::Checkbox`, `ui::Radio`, `ui::ListRow`, `ui::SectionHeader`, `ui::MenuItem`, `ui::Tooltip`).
- **French punctuation**: a no-break space (U+00A0) before `:`, `?`, `!` and `%`; the test fails on a plain space there.
- **Values** are formatted by `ui/components/format.h` (`docs/launcher_ui.md`, "Consistency"), with three keys that are not
  words: `.` is the decimal mark (French `,`), `,` the thousands separator (French a narrow no-break space), `{0} {1} {2}` the
  order of a date's day, month and year; the months are `Jan` to `Dec`.
- Keep a translated string in a `std::string` rather than a `const char*` past the frame: the pointer `Tr` returns is valid until the
  language changes.
- Window text is translated where it is drawn, every frame; text made once and kept (a toast, a disc check's message) is
  translated when it is made.

## The Collection page
The Collection page, the figure sheet, Add a figure and Manage (`ui/collection_*.cpp`) go through `Tr` like the rest. Three
things are particular to them:
- **The library's names** (elements, types, versions) are English in the shared library; the page translates them with
  `ElementName`, `CategoryLabel` and `KindLabel` (tables of `N_`). The library's own French (`LevelText`, `SeatTag`) is
  asked for with `French()`.
- **Data stays as it is**: the catalogue's toy and version names, Giants' hat names, and a figure's own label. A swapper's name
  ("Doom Stone", "Magna Jet") is data too; the words around it ("Swapper", "Top: ... · Bottom: ...", "Change the
  bottom", "Change the top") are translated, with the Toy Box's French ("Changer le bas", "Changer le haut").
- **A count with a plural** is two strings (`{} figure`, `{} figures`), as everywhere.
