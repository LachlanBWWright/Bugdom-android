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

// SDL3 provides SDL_EnumerateDirectory for Android asset enumeration
static bool ExtractDir(const char *assetPrefix, const char *destBase);

typedef struct
{
    const char *assetPrefix;
    const char *destBase;
    bool        ok;
} EnumCtx;

static SDL_EnumerationResult SDLCALL EnumCallback(void *userdata, const char *dirname, const char *fname)
{
    EnumCtx *ctx = (EnumCtx *)userdata;

    char assetPath[1024];
    char destPath[1024];

    // Build the full asset path
    if (dirname && dirname[0])
        snprintf(assetPath, sizeof(assetPath), "%s/%s", dirname, fname);
    else
        snprintf(assetPath, sizeof(assetPath), "%s", fname);

    // Build destination path: strip the assetPrefix from dirname, keep rest
    // assetPrefix: e.g. ""  (assets root is already the data dir)
    // dirname: e.g. "Data/System"
    // destBase: e.g. "/data/data/io.jor.bugdom/files"
    const char *rel = assetPath;
    if (ctx->assetPrefix && ctx->assetPrefix[0])
    {
        size_t pfxLen = strlen(ctx->assetPrefix);
        if (strncmp(assetPath, ctx->assetPrefix, pfxLen) == 0)
            rel = assetPath + pfxLen + (assetPath[pfxLen] == '/' ? 1 : 0);
    }
    snprintf(destPath, sizeof(destPath), "%s/%s", ctx->destBase, rel);

    // Try to extract as file; if it fails, treat as directory and recurse
    if (!ExtractFile(assetPath, destPath))
    {
        // Might be a directory – recurse
        char subAsset[1024];
        snprintf(subAsset, sizeof(subAsset), "%s", assetPath);
        mkdir(destPath, 0755);
        EnumCtx subCtx = { NULL, ctx->destBase, true };
        SDL_EnumerateDirectory(subAsset, EnumCallback, &subCtx);
        if (!subCtx.ok) ctx->ok = false;
    }

    return SDL_ENUM_CONTINUE;
}

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
            // Trim newline
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

    EnumCtx ctx;
    ctx.assetPrefix = prefix;
    ctx.destBase    = destDir;
    ctx.ok          = true;

    if (!SDL_EnumerateDirectory(prefix ? prefix : "", EnumCallback, &ctx) || !ctx.ok)
    {
        LOGE("Asset extraction failed");
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
