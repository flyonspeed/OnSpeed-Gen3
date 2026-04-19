// onspeed_core.h — library discovery anchor
//
// Arduino IDE discovers a library by finding one of its headers via
// #include. Because onspeed_core organises its headers into subfolders
// (ahrs/, audio/, sensors/, types/, util/, ...), consumers cannot
// discover the library by including a subfolder header directly — the
// resolver needs to see a root-level header first.
//
// Including <onspeed_core.h> once (typically from Globals.h) anchors
// discovery so that subsequent <subfolder/Header.h> includes resolve.
//
// This file intentionally exports nothing. Consumers still include
// specific headers from subfolders (e.g. <types/ImuSample.h>).

#ifndef ONSPEED_CORE_H
#define ONSPEED_CORE_H

#endif
