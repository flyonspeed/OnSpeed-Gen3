// FileSys.h

#ifndef FILESYS_H
#define FILESYS_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>

#include <vector>

#include <Arduino.h>
#include <SPI.h>

#ifndef DISABLE_FS_H_WARNING
#define DISABLE_FS_H_WARNING
#endif
#include "SdFat.h"

class SdFileSys
{
public:
    SdFileSys();

    struct SuFileInfo
        {
        // Holds either a root-level CSV/.dbg/.meta or a longer
        // /coredumps/coredump_NNNN_<version>_<task>.bin path (up to 128
        // bytes total: kCoredumpDir + the worst-case version + task
        // suffix BootDiagnostics emits).
        char        szFileName[128];
        uint64_t    uFileSize;
        };

    typedef std::vector<SuFileInfo>     SuFileInfoList;

    // Variables
protected:
    SPIClass        uSD_SPI;        // SD card SPI interface
    SdFat           uSD_FAT;        // Fat file system object
    SdCard        * puSD_Card;      // Low level SD card access (from uSD_FAT.card())
    SdSpiConfig     SpiConfig;

public:
    bool            bSdAvailable;

    // Methods
public:
	bool Init();
    void Info();
    bool FileList(SuFileInfoList * psuFileInfoList);

    // List files (not directories) in a specific path. Returns true even
    // if the path doesn't exist (the result is just an empty vector) so
    // callers can defer "does this directory exist" decisions to the
    // emptiness of the result. Filenames returned are the basename only,
    // not the full path.
    bool DirList(const char * szPath, SuFileInfoList * psuFileInfoList);

    bool Format(Print * pStatusOut = nullptr, float * pSizeGb = nullptr);

    // Some convenience functions
    // Be sure these are wrapped in a semaphore before calling
    bool exists(const char * szFilename) { return uSD_FAT.exists(szFilename); }
    bool exists(const String  sFilename) { return uSD_FAT.exists( sFilename); }
    FsFile open(const char * szFilename, oflag_t iFlags) { return uSD_FAT.open(szFilename, iFlags); }
    bool remove(const char * szFilename);

    // Atomic-ish rename via SdFat. Returns true on success. Caller must
    // hold xWriteMutex — same convention as remove().
    bool rename(const char* srcName, const char* dstName);

    // Create directory (with parents). Returns true on success or if the
    // directory already exists. Caller must hold xWriteMutex.
    bool mkdir(const char* szPath) { return uSD_FAT.mkdir(szPath, /*pFlag=*/true); }
};

#endif

