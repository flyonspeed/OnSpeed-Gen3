//
//
//
#include <iostream>
#include <cstring>

#include "src/Globals.h"

using namespace std;

bool CompareByFileName(const SdFileSys::SuFileInfo & a, SdFileSys::SuFileInfo & b);

// ----------------------------------------------------------------------------
// Class FileSys
// ----------------------------------------------------------------------------

SdFileSys::SdFileSys() :
        uSD_SPI(HSPI),
        // NOTE: do NOT add DEDICATED_SPI here. SdFat's dedicated-SPI mode
        // skips writeStop() after multi-block writes (see SdSpiCard::writeSectors),
        // which leaves Arduino-ESP32's SPIClass paramLock held by the writer
        // task. A subsequent SD access from any other task (e.g. the `list`
        // console command, web file listing, config reload) eventually calls
        // spiStop() and tries to give paramLock from a task that doesn't own
        // it, tripping the FreeRTOS `xTaskPriorityDisinherit` assert and
        // crashing the board.
        // 20 MHz: bench measurement on Phil's V4P shows ~4pp Core 0 win
        // and 64% faster log_sync vs 10 MHz, with no drops and no tail
        // regression.  25 MHz tested but adds tail-latency noise (Log
        // task loops/s range widens from 182..208 to 79..208 — a real
        // SD stall) without buying meaningful steady-state improvement,
        // so 20 MHz is the sweet spot for this card on this board.
        // SdFat auto-negotiates down at init if a card can't sustain
        // this clock; worst case is fallback to a lower speed.
        SpiConfig(kSdCs, USER_SPI_BEGIN, SD_SCK_MHZ(20), &uSD_SPI)
    {
    }

// ----------------------------------------------------------------------------

// Initialize SD card

bool SdFileSys::Init()
    {
    // Init SD SPI interface
    if (!uSD_SPI.begin(kSdSclk, kSdMiso, kSdMosi, kSdCs))
        return false;

    // Init FAT file system object
    bSdAvailable = uSD_FAT.begin(SpiConfig);

    // Get the card pointer from the already-initialized FAT object.
    // Do NOT construct a separate SdCardFactory — SdFat already owns an
    // internal SdSpiCard initialized by uSD_FAT.begin() above; creating a
    // second one diverges the card state from what SdFat actually uses for
    // I/O (and caused the original "SD open failed" boot-time regression).
    puSD_Card = uSD_FAT.card();

    return bSdAvailable;
    }

// ----------------------------------------------------------------------------

void SdFileSys::Info()
    {
    uint32_t cardSectorCount = puSD_Card->sectorCount();
    if (!cardSectorCount)
        {
        g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "Get sector count failed.");
        return;
        }

    g_Log.printf("\nCard size: %f GB\n", cardSectorCount * 5.12e-7);

    g_Log.print("Card will be formatted ");
    if      (cardSectorCount > 67108864)  g_Log.printf("exFAT\n");
    else if (cardSectorCount >  4194304)  g_Log.printf("FAT32\n");
    else                                  g_Log.printf("FAT16\n");

    }

// ----------------------------------------------------------------------------

// Return an array of info about files on the SD disk

bool SdFileSys::FileList(SuFileInfoList * psuFileInfoList)
    {
    return DirList("/", psuFileInfoList);
    }

bool SdFileSys::DirList(const char * szPath, SuFileInfoList * psuFileInfoList)
    {
    FsFile      hDir;
    FsFile      hFileEntry;
    SuFileInfo  suFileInfo;

    psuFileInfoList->clear();

    hDir = uSD_FAT.open(szPath);
    if (!hDir.isOpen())
        {
        // Caller treats "directory missing" identically to "directory empty" —
        // useful for /coredumps which only exists after the first archival.
        return true;
        }
    if (!hDir.isDir())
        {
        hDir.close();
        return false;
        }

    while(true)
        {
        hFileEntry =  hDir.openNextFile();

        if (!hFileEntry.isOpen())
            {
            // no more files
            break;
            }

        if (!hFileEntry.isDirectory())
            {
            // Only list files, not subdirectories.
            hFileEntry.getName(suFileInfo.szFileName, sizeof(suFileInfo.szFileName));
            suFileInfo.uFileSize = hFileEntry.fileSize();
            psuFileInfoList->push_back(suFileInfo);
            }

        hFileEntry.close();
        }

    hDir.close();

    // Sort by filename so callers (and humans reading /api/logs) get a
    // deterministic order.
    std::sort(psuFileInfoList->begin(), psuFileInfoList->end(), CompareByFileName);

    return true;
    }


// ----------------------------------------------------------------------------

// Return an array of info about files on the SD disk

bool SdFileSys::Format(Print * pStatusOut, float * pSizeGb)
    {
    uint32_t        uCardSectorCount;
    uint8_t         auSectorBuffer[512];
    ExFatFormatter  exFatFormatter;
    FatFormatter    fatFormatter;

    // Make sure we can talk to the SD card
    if ((!puSD_Card || puSD_Card->errorCode()))
        {
        if (pStatusOut != nullptr)
            {
            pStatusOut->print("FORMAT ERROR: Cannot initialize SD card. ");
            pStatusOut->println(puSD_Card->errorCode());
            }
        return false;
        }

    // Gracefully close the FAT file system driver
//    uSD_FAT.end();

    // SD card seems to be available
    uCardSectorCount = puSD_Card->sectorCount();
    if (!uCardSectorCount)
        {
        g_Log.println(MsgLog::EnDisk, MsgLog::EnError, "Get sector count failed.");
        if (pStatusOut != nullptr)
            pStatusOut->println("FORMAT ERROR: Could not read sector count.");
        return false;
        }

    // If the caller wants the card size, give it to them now — even if
    // the format itself fails later, the size we read from the card is
    // still valid information.
    if (pSizeGb != nullptr)
        *pSizeGb = uCardSectorCount * 5.12e-7f;

    //if (pStatusOut != nullptr)
    //    {
    //    pStatusOut->print("Card sectors: ");
    //    pStatusOut->println(uCardSectorCount);
    //    pStatusOut->print("Card size:    ");
    //    pStatusOut->print(uCardSectorCount * 5.12e-7);
    //    pStatusOut->println(" GBytes");
    //    }

    // The exFAT formatter writes its own boot sector, FAT, and root
    // cluster from scratch, so no pre-erase is needed.

    // Format the card
    // Format exFAT if larger than 32GB.
    bool bFormatStatus = uCardSectorCount > 67108864 ?
        exFatFormatter.format(puSD_Card, auSectorBuffer, pStatusOut) :
        fatFormatter.format  (puSD_Card, auSectorBuffer, pStatusOut);
    if (!bFormatStatus)
        {
        if (pStatusOut != nullptr)
            pStatusOut->println("FORMAT ERROR: Could not format SD card.");
        return false;
        }

    if (pStatusOut != nullptr)
        {
        pStatusOut->print("SD card format completed. Card size: ");
        pStatusOut->print(uCardSectorCount * 5.12e-7);
        pStatusOut->println("GBytes");
        }
    delay(300);

    // Format OK so mount FAT file system
    int iSdBeginTries = 0;
    bSdAvailable = uSD_FAT.begin(SpiConfig);
    while (!bSdAvailable && iSdBeginTries < 5)
        {
        delay(200);

        // Try up to 5 times
        //pStatusOut->println("Reintializing SD card.");
        bSdAvailable = uSD_FAT.begin(SpiConfig);
        iSdBeginTries++;
        }

    if ((!bSdAvailable) && (pStatusOut != nullptr))
        pStatusOut->println("SD card couldn't be initialized");

    return bSdAvailable;
    } // end Format()


// ----------------------------------------------------------------------------

bool SdFileSys::remove(const char * szFilename)
    {
    return uSD_FAT.remove(szFilename);
    }

// ----------------------------------------------------------------------------

bool SdFileSys::rename(const char* srcName, const char* dstName)
    {
    return uSD_FAT.rename(srcName, dstName);
    }

// ============================================================================

// Comparison function sorting directory file lists

bool CompareByFileName(const SdFileSys::SuFileInfo & a, SdFileSys::SuFileInfo & b)
    {
    // For now it is just a simple case insensitive compare
    return strcasecmp(a.szFileName, b.szFileName) < 0;
    }
