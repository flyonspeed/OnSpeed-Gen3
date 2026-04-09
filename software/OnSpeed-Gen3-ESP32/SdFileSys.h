// FileSys.h

#ifndef FILESYS_H
#define FILESYS_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>

#include <vector>

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif

#include <SPI.h>
#ifndef DISABLE_FS_H_WARNING
#define DISABLE_FS_H_WARNING
#endif
#include <SdFat.h>

class SdFileSys
{
public:
    SdFileSys();

    struct SuFileInfo
        {
        char        szFileName[25];
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
    bool Format(Print * pStatusOut = nullptr, bool bErase = true);

    // Some convenience functions
    // Be sure these are wrapped in a semaphore before calling
    bool exists(const char * szFilename) { return uSD_FAT.exists(szFilename); }
    bool exists(const String  sFilename) { return uSD_FAT.exists( sFilename); }
    FsFile open(const char * szFilename, oflag_t iFlags) { return uSD_FAT.open(szFilename, iFlags); }
    bool remove(const char * szFilename);
};

#endif

