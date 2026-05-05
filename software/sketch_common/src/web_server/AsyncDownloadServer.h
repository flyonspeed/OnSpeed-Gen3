#ifndef ASYNC_DOWNLOAD_SERVER_H
#define ASYNC_DOWNLOAD_SERVER_H

// Spike: stand up an ESPAsyncWebServer on a separate port that mirrors
// only the /download endpoint, so /bench can A/B steady-state download
// throughput against the synchronous server on port 80 without having
// to flash two different builds.

void AsyncDownloadServerInit();

#endif
