//
//
//
#include <iostream>
#include <cstring>

#include "Globals.h"
#include "SdFileSys.h"

using namespace std;

bool CompareByFileName(const SdFileSys::SuFileInfo & a, SdFileSys::SuFileInfo & b);

// ----------------------------------------------------------------------------
// Class FileSys
// ----------------------------------------------------------------------------

SdFileSys::SdFileSys() :
        uSD_SPI(HSPI),
        SpiConfig(SD_CS, USER_SPI_BEGIN,  1000000UL * 10 /*SdSpiConfig::maxSck*/, &uSD_SPI)
    {
    }

// ----------------------------------------------------------------------------

// Initialize SD card
// ------------------
/*  Need to look into something called dedicated SPI.
    https://github.com/greiman/SdFat/issues/244
    says "The only way to get dedicated SPI is to explicitly specify
    DEDICATED_SPI using a begin call with SdSpiConfig as the argument like this..."
        #define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
        if (!sd.begin(SD_CONFIG))
            {
            // handle error
            }

    The problem is that I don't know how to specify custom pins using SdSpiConfig().
    Maybe like this...

    SoftSpiDriver<SOFT_MISO_PIN, SOFT_MOSI_PIN, SOFT_SCK_PIN> softSpi;
    SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)

BTW, this could possibly go in a task so that if someone inserts a card
while powered up then good things would happen.
*/

bool SdFileSys::Init()
    {
    // Init SD SPI interface
    if (!uSD_SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS))
        return false;

    // Init FAT file system object
    bSdAvailable = uSD_FAT.begin(SpiConfig);

    // Init low level card stuff
    puSD_Card = CardFactory.newCard(SpiConfig);

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

bool SdFileSys::Format(Print * pStatusOut, bool bErase)
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

    //if (pStatusOut != nullptr)
    //    {
    //    pStatusOut->print("Card sectors: ");
    //    pStatusOut->println(uCardSectorCount);
    //    pStatusOut->print("Card size:    ");
    //    pStatusOut->print(uCardSectorCount * 5.12e-7);
    //    pStatusOut->println(" GBytes");
    //    }

    // Do optional erase. I'm not sure what the need for this is.
    if (bErase)
        {
        uint32_t const  ERASE_SIZE  = 262144L;
        uint32_t        uFirstBlock = 0;
        uint32_t        uLastBlock;
        do {
            uLastBlock = uFirstBlock + ERASE_SIZE - 1;

            if (uLastBlock >= uCardSectorCount)
                uLastBlock = uCardSectorCount - 1;

            if (!puSD_Card->erase(uFirstBlock, uLastBlock))
                if (pStatusOut != nullptr)
                    pStatusOut->println("Card erase failed");

            uFirstBlock += ERASE_SIZE;
            } while (uFirstBlock < uCardSectorCount);
        } // end if erase card

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

// ============================================================================

// Comparison function sorting directory file lists

bool CompareByFileName(const SdFileSys::SuFileInfo & a, SdFileSys::SuFileInfo & b)
    {
    // For now it is just a simple case insensitive compare
    return strcasecmp(a.szFileName, b.szFileName) < 0;
    }
