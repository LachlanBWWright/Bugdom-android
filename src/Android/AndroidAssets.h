// ANDROID ASSET EXTRACTION
// Extracts game data from APK assets to internal storage on first run.
#pragma once

#ifdef __ANDROID__

#ifdef __cplusplus
extern "C" {
#endif

// Extract all assets under "prefix" into destDir.
// Returns true on success, false on failure.
// This should be called once at startup before the game tries to open files.
bool Android_ExtractAssets(const char *destDir, const char *prefix);

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__
