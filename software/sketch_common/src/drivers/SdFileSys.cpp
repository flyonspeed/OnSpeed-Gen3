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
        SpiConfig(kSdCs, USER_SPI_BEGIN, SD_SCK_MHZ(10), &uSD_SPI)
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
    FsFile      hRootDir;
    FsFile      hFileEntry;
    SuFileInfo  suFileInfo;

    psuFileInfoList->clear();

    hRootDir = uSD_FAT.open("/");
    if (!hRootDir.isOpen())
        return false;

    while(true)
        {
        hFileEntry =  hRootDir.openNextFile();

        if (!hFileEntry.isOpen())
            {
            // no more files
            break;
            }

        if (!hFileEntry.isDirectory())
            {
            // Only list files in root folder, no directories
            hFileEntry.getName(suFileInfo.szFileName, sizeof(suFileInfo.szFileName));
            suFileInfo.uFileSize = hFileEntry.fileSize();
            psuFileInfoList->push_back(suFileInfo);
            }

        hFileEntry.close();
        }

    // Put file names in order
    std::sort(psuFileInfoList->begin(), psuFileInfoList->end(), CompareByFileName);

    return true;
    }


// ----------------------------------------------------------------------------

// Return an array of info about files on the SD disk

bool SdFileSys::Format(Print * pStatusOut, bool bErase, float * pSizeGb)
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

    // Pre-erase is intentionally a no-op. The puSD_Card->erase() loop
    // (256K-block chunks, no vTaskDelay/watchdog feed) takes multiple
    // minutes on a 64 GB card at SPI@10MHz — long enough for a
    // task-watchdog or brownout reset to interrupt the format and leave
    // the FS half-zeroed. The exFAT formatter writes its own boot sector,
    // FAT, and root cluster regardless of whether the underlying flash was
    // pre-erased; pre-erase is a TRIM hint, not a correctness requirement.
    // bErase is kept for callers that might pass it explicitly, but the
    // body does nothing.
    if (bErase)
        {
        g_Log.println(MsgLog::EnDisk, MsgLog::EnWarning,
            "Format: pre-erase requested but skipped (no-op; see SdFileSys::Format).");
        if (pStatusOut != nullptr)
            pStatusOut->println("Note: pre-erase requested but skipped (see SdFileSys::Format).");
        }

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
