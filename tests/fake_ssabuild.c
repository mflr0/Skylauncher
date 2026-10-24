/* A stand-in for the player kit's SSABuild (docs/install.md): prints its events with delays, so the launcher's install
 * screens can be tried without the real kit. Built as fake-kit/SSABuild in the build folder (target FakeSSABuild);
 * point the launcher at that folder with SKYLAUNCHER_KIT.
 * Same steps, titles and order as the real one: toolchain, disc, recompile, shaders, compile, link, movies, then the
 * second shaders/compile/link pass that follows the movie shaders.
 *   FAKE_SSABUILD_FAIL=<step id>  fail half way through that step (toolchain, disc, recompile, shaders, compile, link,
 *                                 movies)
 *   FAKE_SSABUILD_DELAY=<seconds> pause between events (default 0.05) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#define MAKE_DIR(path) _mkdir(path)
#define GAME "SSARecomp.exe"
#else
#include <sys/stat.h>
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#define MAKE_DIR(path) mkdir(path, 0755)
#define GAME "SSARecomp"
#endif

static int g_delay = 50;

static void step(const char* id, const char* title, int count)
{
    const char* fail = getenv("FAKE_SSABUILD_FAIL");
    printf("@step %s %s\n", id, title);
    for (int i = 1; i <= count; i++)
    {
        printf("@progress %d %d\n", i, count);
        if (i % 10 == 0)
            printf("@log %s: file %d of %d\n", id, i, count);
        if (fail != NULL && strcmp(fail, id) == 0 && i == count / 2)
        {
            fflush(stdout);
            fprintf(stderr, "clang: error: something went wrong in item %d\n", i);
            fflush(stderr);
            printf("@error %s failed: item %d didn't compile.\n", title, i);
            exit(1);
        }
        fflush(stdout);
        SLEEP_MS(g_delay);
    }
}

int main(int argc, char** argv)
{
    const char *iso = NULL, *out = NULL;
    for (int i = 1; i + 1 < argc; i++)
    {
        if (strcmp(argv[i], "--iso") == 0)
            iso = argv[++i];
        else if (strcmp(argv[i], "--out") == 0)
            out = argv[++i];
    }
    if (getenv("FAKE_SSABUILD_DELAY") != NULL)
        g_delay = (int)(atof(getenv("FAKE_SSABUILD_DELAY")) * 1000);
    if (iso == NULL || out == NULL)
    {
        printf("@error usage: SSABuild --iso <iso> --out <game dir>\n");
        return 2;
    }

    printf("@log fake SSABuild, iso %s\n", iso);
    step("toolchain", "Preparing the compiler", 10);
    step("disc", "Reading the disc", 10);
    step("recompile", "Translating the game code", 40);
    step("shaders", "Translating the shaders", 30);
    step("compile", "Compiling the game", 80);
    step("link", "Linking the game", 5);
    step("movies", "Adding the movie shaders", 10);
    step("shaders", "Translating the shaders", 5);
    step("compile", "Compiling the game", 5);
    step("link", "Linking the game", 2);
    printf("@log finished\n");

    char path[4096];
    MAKE_DIR(out);
    snprintf(path, sizeof(path), "%s/%s", out, GAME);
    FILE* game = fopen(path, "wb");
    if (game == NULL)
    {
        printf("@error can't write %s\n", path);
        return 1;
    }
    fputs("fake game\n", game);
    fclose(game);
    printf("@done %s\n", path);
    return 0;
}
