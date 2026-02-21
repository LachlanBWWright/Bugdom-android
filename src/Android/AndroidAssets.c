// ANDROID ASSET EXTRACTION IMPLEMENTATION
// Copies game data from APK assets to the app's internal storage.
//
// SDL_EnumerateDirectory uses POSIX opendir() on Android and therefore
// CANNOT enumerate APK asset paths.  Instead we keep a complete, explicit
// list of every game data file.  SDL_IOFromFile() with a relative path
// DOES read from the APK asset bundle on Android, so we use that for the
// actual byte-for-byte copy.

#ifdef __ANDROID__

#include "AndroidAssets.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  "Bugdom", __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "Bugdom", __VA_ARGS__)

// Version file: if this file exists and contains our version, skip extraction.
// Bump this string whenever the Data/ directory contents change.
#define EXTRACT_VERSION_FILE  ".extract_version"
#define EXTRACT_VERSION       "1.3.5"

// -------------------------------------------------------------------------
// Complete list of all game data files, relative to the Data/ root.
// These are the exact paths that end up at the APK asset bundle root
// (because build.gradle.kts uses  assets.srcDirs("../../Data")).
// -------------------------------------------------------------------------
static const char *kAllDataFiles[] = {
    "Audio/AntHill.sounds/Explosion.aiff",
    "Audio/AntHill.sounds/FireCrackle.aiff",
    "Audio/AntHill.sounds/Laugh.aiff",
    "Audio/AntHill.sounds/PipeClang.aiff",
    "Audio/AntHill.sounds/Shoot.aiff",
    "Audio/AntHill.sounds/Sizzle.aiff",
    "Audio/AntHill.sounds/ValveOpen.aiff",
    "Audio/AntHill.sounds/WaterLeak.aiff",
    "Audio/AntHillSong.aiff",
    "Audio/Bonus.sounds/Bell.aiff",
    "Audio/Bonus.sounds/Click.aiff",
    "Audio/BonusSong.aiff",
    "Audio/Forest.aiff",
    "Audio/Forest.sounds/Explosion.aiff",
    "Audio/Forest.sounds/FireCrackle.aiff",
    "Audio/Forest.sounds/Footstep.aiff",
    "Audio/Forest.sounds/Helicopter.aiff",
    "Audio/Forest.sounds/Plasmaburst.aiff",
    "Audio/HighScores.aiff",
    "Audio/Hive.sounds/Plunger.aiff",
    "Audio/Hive.sounds/Pump.aiff",
    "Audio/Hive.sounds/StingerShoot.aiff",
    "Audio/HiveLevel.aiff",
    "Audio/Lawn.sounds/DoorOpen.aiff",
    "Audio/LawnSong.aiff",
    "Audio/LawnSongOld.aiff",
    "Audio/LoseSong.aiff",
    "Audio/Main.sounds/BuddyLaunch.aiff",
    "Audio/Main.sounds/Checkpoint.aiff",
    "Audio/Main.sounds/Firecracker.aiff",
    "Audio/Main.sounds/FlyBuzz.aiff",
    "Audio/Main.sounds/GetHit.aiff",
    "Audio/Main.sounds/GetPOW.aiff",
    "Audio/Main.sounds/HitDirt.aiff",
    "Audio/Main.sounds/Jump.aiff",
    "Audio/Main.sounds/Kablam.aiff",
    "Audio/Main.sounds/Kick.aiff",
    "Audio/Main.sounds/LadyBugRescue.aiff",
    "Audio/Main.sounds/Morph.aiff",
    "Audio/Main.sounds/Pop.aiff",
    "Audio/Main.sounds/Pound.aiff",
    "Audio/Main.sounds/Select.aiff",
    "Audio/Main.sounds/Shield.aiff",
    "Audio/Main.sounds/SpeedBoost.aiff",
    "Audio/Main.sounds/Splash.aiff",
    "Audio/Main.sounds/ThrowSpear.aiff",
    "Audio/MenuSong.aiff",
    "Audio/Night.aiff",
    "Audio/Night.sounds/DoorOpen.aiff",
    "Audio/Night.sounds/RockSlam.aiff",
    "Audio/Pond.sounds/BoatEngine.aiff",
    "Audio/Pond.sounds/Slurp.aiff",
    "Audio/Pond.sounds/Waterbug.aiff",
    "Audio/PondSong.aiff",
    "Audio/Song_Pangea.aiff",
    "Audio/WinSong.aiff",
    "Images/Infobar/128.tga",
    "Images/Infobar/129.tga",
    "Images/Infobar/130.tga",
    "Images/Infobar/131.tga",
    "Images/Infobar/132.tga",
    "Images/Infobar/133.tga",
    "Images/Infobar/134.tga",
    "Images/Infobar/135.tga",
    "Images/Infobar/136.tga",
    "Images/Infobar/137.tga",
    "Images/Infobar/138.tga",
    "Images/Infobar/139.tga",
    "Images/Infobar/140.tga",
    "Images/Infobar/141.tga",
    "Images/Infobar/142.tga",
    "Images/Infobar/143.tga",
    "Images/Infobar/144.tga",
    "Images/Infobar/145.tga",
    "Images/Infobar/146.tga",
    "Images/Infobar/147.tga",
    "Images/Infobar/148.tga",
    "Images/Infobar/149.tga",
    "Images/Infobar/150.tga",
    "Images/Infobar/151.tga",
    "Images/Infobar/152.tga",
    "Images/Infobar/153.tga",
    "Images/Infobar/154.tga",
    "Images/Infobar/155.tga",
    "Images/Infobar/156.tga",
    "Images/Infobar/157.tga",
    "Images/Infobar/158.tga",
    "Images/Infobar/NitroGauge.tga",
    "Images/Textures/1000.tga",
    "Images/Textures/1001.tga",
    "Images/Textures/1002.tga",
    "Images/Textures/1003.tga",
    "Images/Textures/1004.tga",
    "Images/Textures/128.tga",
    "Images/Textures/129.tga",
    "Images/Textures/130.tga",
    "Images/Textures/131.tga",
    "Images/Textures/132.tga",
    "Images/Textures/133.tga",
    "Images/Textures/134.tga",
    "Images/Textures/135.tga",
    "Images/Textures/136.tga",
    "Images/Textures/137.tga",
    "Images/Textures/1500.tga",
    "Images/Textures/1501.tga",
    "Images/Textures/1502.tga",
    "Images/Textures/1503.tga",
    "Images/Textures/200.tga",
    "Images/Textures/2000.tga",
    "Images/Textures/2001.tga",
    "Images/Textures/2002.tga",
    "Images/Textures/2003.tga",
    "Images/Textures/2004.tga",
    "Images/Textures/2005.tga",
    "Images/Textures/2006.tga",
    "Images/Textures/2007.tga",
    "Images/Textures/2008.tga",
    "Images/Textures/201.tga",
    "Images/Textures/202.tga",
    "Images/Textures/3000.sfl",
    "Images/Textures/3000.tga",
    "Images/Textures/3500.tga",
    "Images/Textures/3510.tga",
    "Images/Textures/3511.tga",
    "Images/Textures/3512.tga",
    "Images/Textures/3513.tga",
    "Images/Textures/3514.tga",
    "Images/Textures/3515.tga",
    "Images/Textures/3516.tga",
    "Images/Textures/3517.tga",
    "Images/Textures/3518.tga",
    "Images/Textures/3519.tga",
    "Models/AntHill_Models.3dmf",
    "Models/BeeHive_Models.3dmf",
    "Models/BonusScreen.3dmf",
    "Models/Forest_Models.3dmf",
    "Models/Global_Models1.3dmf",
    "Models/Global_Models2.3dmf",
    "Models/HighScores.3dmf",
    "Models/Lawn_Models1.3dmf",
    "Models/Lawn_Models2.3dmf",
    "Models/LevelIntro.3dmf",
    "Models/MainMenu.3dmf",
    "Models/Night_Models.3dmf",
    "Models/Pangea.3dmf",
    "Models/Pond_Models.3dmf",
    "Models/Title.3dmf",
    "Models/WinLose.3dmf",
    "Skeletons/Ant.3dmf",
    "Skeletons/Ant.skeleton.rsrc",
    "Skeletons/AntKing.3dmf",
    "Skeletons/AntKing.skeleton.rsrc",
    "Skeletons/Bat.3dmf",
    "Skeletons/Bat.skeleton.rsrc",
    "Skeletons/BoxerFly.3dmf",
    "Skeletons/BoxerFly.skeleton.rsrc",
    "Skeletons/Buddy.3dmf",
    "Skeletons/Buddy.skeleton.rsrc",
    "Skeletons/Caterpillar.3dmf",
    "Skeletons/Caterpillar.skeleton.rsrc",
    "Skeletons/DoodleBug.3dmf",
    "Skeletons/DoodleBug.skeleton.rsrc",
    "Skeletons/DragonFly.3dmf",
    "Skeletons/DragonFly.skeleton.rsrc",
    "Skeletons/FireFly.3dmf",
    "Skeletons/FireFly.skeleton.rsrc",
    "Skeletons/FlyingBee.3dmf",
    "Skeletons/FlyingBee.skeleton.rsrc",
    "Skeletons/Foot.3dmf",
    "Skeletons/Foot.skeleton.rsrc",
    "Skeletons/LadyBug.3dmf",
    "Skeletons/LadyBug.skeleton.rsrc",
    "Skeletons/Larva.3dmf",
    "Skeletons/Larva.skeleton.rsrc",
    "Skeletons/Mosquito.3dmf",
    "Skeletons/Mosquito.skeleton.rsrc",
    "Skeletons/PondFish.3dmf",
    "Skeletons/PondFish.skeleton.rsrc",
    "Skeletons/QueenBee.3dmf",
    "Skeletons/QueenBee.skeleton.rsrc",
    "Skeletons/Roach.3dmf",
    "Skeletons/Roach.skeleton.rsrc",
    "Skeletons/RootSwing.3dmf",
    "Skeletons/RootSwing.skeleton.rsrc",
    "Skeletons/Skippy.3dmf",
    "Skeletons/Skippy.skeleton.rsrc",
    "Skeletons/Slug.3dmf",
    "Skeletons/Slug.skeleton.rsrc",
    "Skeletons/Spider.3dmf",
    "Skeletons/Spider.skeleton.rsrc",
    "Skeletons/WaterBug.3dmf",
    "Skeletons/WaterBug.skeleton.rsrc",
    "Skeletons/WingedFireAnt.3dmf",
    "Skeletons/WingedFireAnt.skeleton.rsrc",
    "Skeletons/WorkerBee.3dmf",
    "Skeletons/WorkerBee.skeleton.rsrc",
    "System/gamecontrollerdb.txt",
    "Terrain/AntHill.ter.rsrc",
    "Terrain/AntKing.ter.rsrc",
    "Terrain/Beach.ter.rsrc",
    "Terrain/BeeHive.ter.rsrc",
    "Terrain/Flight.ter.rsrc",
    "Terrain/Lawn.ter.rsrc",
    "Terrain/Night.ter.rsrc",
    "Terrain/Pond.ter.rsrc",
    "Terrain/QueenBee.ter.rsrc",
    "Terrain/Training.ter.rsrc",
    NULL
};

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

// Create every directory component of a file path.
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

// Copy one file from the APK asset bundle to the filesystem.
// assetPath  – relative to the APK asset root (no leading /)
// destPath   – absolute filesystem destination
static bool ExtractOneFile(const char *assetPath, const char *destPath)
{
    // SDL_IOFromFile with a relative path opens directly from the APK assets
    // on Android (uses AAssetManager internally).
    SDL_IOStream *src = SDL_IOFromFile(assetPath, "rb");
    if (!src)
    {
        LOGE("Cannot open asset %s: %s", assetPath, SDL_GetError());
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
// Public API
// -------------------------------------------------------------------------

bool Android_ExtractAssets(const char *destDir)
{
    // Check if already extracted with the current version.
    // The version file is only written after a complete successful extraction,
    // so a partial extraction (e.g. after a crash mid-way) will be retried.
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

    int totalFiles = 0;
    int failedFiles = 0;

    for (int i = 0; kAllDataFiles[i] != NULL; i++)
    {
        char destPath[1024];
        snprintf(destPath, sizeof(destPath), "%s/%s", destDir, kAllDataFiles[i]);

        if (!ExtractOneFile(kAllDataFiles[i], destPath))
        {
            LOGE("Failed to extract %s", kAllDataFiles[i]);
            failedFiles++;
        }

        totalFiles++;
    }

    if (failedFiles > 0)
    {
        LOGE("Asset extraction: %d/%d files failed", failedFiles, totalFiles);
        return false;
    }

    // Write version stamp only after all files succeeded.
    vf = fopen(versionFile, "w");
    if (vf)
    {
        fputs(EXTRACT_VERSION "\n", vf);
        fclose(vf);
    }

    LOGI("Asset extraction complete: %d files extracted", totalFiles);
    return true;
}

#endif // __ANDROID__
