// ANDROID ASSET EXTRACTION IMPLEMENTATION
// Copies game data from APK assets to the app's internal storage.
// Uses SDL3's asset I/O API so we don't need direct JNI/AAssetManager access.

#ifdef __ANDROID__

#include "AndroidAssets.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  "Bugdom", __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "Bugdom", __VA_ARGS__)

// Version file: if this file exists and contains our version, skip extraction
#define EXTRACT_VERSION_FILE  ".extract_version"
#define EXTRACT_VERSION       "1.3.5"

static void MakeDirsFor(const char *path)
{
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

static bool ExtractFile(const char *assetPath, const char *destPath)
{
    SDL_IOStream *src = SDL_IOFromFile(assetPath, "rb");
    if (!src)
    {
        // SDL on Android looks in APK assets; if not found, not an error for dirs
        return false;
    }

    MakeDirsFor(destPath);

    FILE *dst = fopen(destPath, "wb");
    if (!dst)
    {
        SDL_CloseIO(src);
        LOGE("Cannot create %s: %s", destPath, strerror(errno));
        return false;
    }

    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = SDL_ReadIO(src, buf, sizeof(buf))) > 0)
    {
        if (fwrite(buf, 1, n, dst) != n)
        {
            LOGE("Write error for %s", destPath);
            ok = false;
            break;
        }
    }

    fclose(dst);
    SDL_CloseIO(src);
    return ok;
}

// -------------------------------------------------------------------------
// Recursive enumeration helpers
// -------------------------------------------------------------------------

// Context for the SDL_EnumerateDirectory callback
typedef struct
{
    char        assetBase[512];   // root asset prefix (may be empty)
    char        destBase[512];    // destination root path
    bool        ok;
} EnumCtx;

// Forward declaration
static bool ExtractDirRecursive(const char *assetDir, const char *destDir);

// SDL callback: called for each entry in a directory
static SDL_EnumerationResult SDLCALL EnumCallback(void *userdata, const char *dirname, const char *fname)
{
    EnumCtx *ctx = (EnumCtx *)userdata;
    if (!ctx->ok) return SDL_ENUM_STOP;

    // Build full asset path and destination path
    char assetPath[1024];
    char destPath[1024];

    if (dirname && dirname[0])
        snprintf(assetPath, sizeof(assetPath), "%s/%s", dirname, fname);
    else
        snprintf(assetPath, sizeof(assetPath), "%s", fname);

    // Map assetPath → destPath by stripping any assetBase prefix
    const char *rel = assetPath;
    if (ctx->assetBase[0])
    {
        size_t pfxLen = strlen(ctx->assetBase);
        if (strncmp(assetPath, ctx->assetBase, pfxLen) == 0)
            rel = assetPath + pfxLen + (assetPath[pfxLen] == '/' ? 1 : 0);
    }
    snprintf(destPath, sizeof(destPath), "%s/%s", ctx->destBase, rel);

    // Try to extract as a file
    if (ExtractFile(assetPath, destPath))
        return SDL_ENUM_CONTINUE;

    // Extraction as file failed – treat as directory and recurse
    mkdir(destPath, 0755);
    if (!ExtractDirRecursive(assetPath, destPath))
        ctx->ok = false;

    return SDL_ENUM_CONTINUE;
}

// Recursively extract assets from assetDir → destDir
static bool ExtractDirRecursive(const char *assetDir, const char *destDir)
{
    mkdir(destDir, 0755);

    EnumCtx ctx;
    SDL_strlcpy(ctx.assetBase, assetDir, sizeof(ctx.assetBase));
    SDL_strlcpy(ctx.destBase,  destDir,  sizeof(ctx.destBase));
    ctx.ok = true;

    if (!SDL_EnumerateDirectory(assetDir, EnumCallback, &ctx))
    {
        // SDL_EnumerateDirectory may fail if assetDir is a file; caller handles that
        return false;
    }

    return ctx.ok;
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

// Known subdirectories in the Bugdom Data folder.
// On Android, AAssetDir_getNextFileName does not list subdirectory names,
// so we must enumerate each known directory explicitly.
// List ALL directories (not just top-level), relative to the Data root.
static const char *kAllDataDirs[] = {
    "Audio",
    "Audio/AntHill.sounds",
    "Audio/Bonus.sounds",
    "Audio/Forest.sounds",
    "Audio/Hive.sounds",
    "Audio/Lawn.sounds",
    "Audio/Main.sounds",
    "Audio/Night.sounds",
    "Audio/Pond.sounds",
    "Images",
    "Images/Infobar",
    "Images/Textures",
    "Models",
    "Skeletons",
    "System",
    "Terrain",
    NULL
};

bool Android_ExtractAssets(const char *destDir, const char *prefix)
{
    // Check if already extracted with current version
    char versionFile[1024];
    snprintf(versionFile, sizeof(versionFile), "%s/%s", destDir, EXTRACT_VERSION_FILE);

    FILE *vf = fopen(versionFile, "r");
    if (vf)
    {
        char ver[64] = "";
        if (fgets(ver, sizeof(ver), vf))
        {
            size_t len = strlen(ver);
            while (len > 0 && (ver[len-1] == '\n' || ver[len-1] == '\r'))
                ver[--len] = '\0';

            if (strcmp(ver, EXTRACT_VERSION) == 0)
            {
                fclose(vf);
                LOGI("Assets already extracted (version %s)", EXTRACT_VERSION);
                return true;
            }
        }
        fclose(vf);
    }

    LOGI("Extracting game assets to %s ...", destDir);
    mkdir(destDir, 0755);

    bool ok = true;

    // Extract each known directory (all directories enumerated explicitly because
    // Android's AAssetDir_getNextFileName does not return subdirectory names).
    for (int i = 0; kAllDataDirs[i] != NULL; i++)
    {
        char assetSubDir[512];
        char destSubDir[512];

        if (prefix && prefix[0])
            snprintf(assetSubDir, sizeof(assetSubDir), "%s/%s", prefix, kAllDataDirs[i]);
        else
            snprintf(assetSubDir, sizeof(assetSubDir), "%s", kAllDataDirs[i]);

        snprintf(destSubDir, sizeof(destSubDir), "%s/%s", destDir, kAllDataDirs[i]);

        LOGI("Extracting %s ...", assetSubDir);
        mkdir(destSubDir, 0755);
        if (!ExtractDirRecursive(assetSubDir, destSubDir))
        {
            LOGE("Failed to extract %s", assetSubDir);
            ok = false;
        }
    }

    if (!ok)
    {
        LOGE("Asset extraction had errors");
        return false;
    }

    // Write version file
    vf = fopen(versionFile, "w");
    if (vf)
    {
        fputs(EXTRACT_VERSION "\n", vf);
        fclose(vf);
    }

    LOGI("Asset extraction complete");
    return true;
}

#endif // __ANDROID__
