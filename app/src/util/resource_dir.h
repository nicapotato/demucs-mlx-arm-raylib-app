#pragma once

#include "raylib.h"

#if defined(__cplusplus)
extern "C" {
#endif
    inline static bool SearchAndSetResourceDir(const char* folderName)
    {
        if (DirectoryExists(folderName))
        {
            ChangeDirectory(TextFormat("%s/%s", GetWorkingDirectory(), folderName));
            return true;
        }

        const char* appDir = GetApplicationDirectory();

        const char* dir = TextFormat("%s%s", appDir, folderName);
        if (DirectoryExists(dir))
        {
            ChangeDirectory(dir);
            return true;
        }

        dir = TextFormat("%s../%s", appDir, folderName);
        if (DirectoryExists(dir))
        {
            ChangeDirectory(dir);
            return true;
        }

        dir = TextFormat("%s../../%s", appDir, folderName);
        if (DirectoryExists(dir))
        {
            ChangeDirectory(dir);
            return true;
        }

        dir = TextFormat("%s../../../%s", appDir, folderName);
        if (DirectoryExists(dir))
        {
            ChangeDirectory(dir);
            return true;
        }

        return false;
    }

#if defined(__cplusplus)
}
#endif
