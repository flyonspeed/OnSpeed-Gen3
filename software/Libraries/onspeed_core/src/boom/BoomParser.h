// BoomParser.h
//
// Pure parser for the OnSpeed boom probe serial output.
//
// Real-world wire format (from a live AirDAQ boom):
//
//   $AIRDAQ,64.12.B8,ADC,SSSS,DDDD,AAAA,BBBB,XX\r\n
//   └─────┘ └──────┘ └─┘ └──┘└──┘└──┘└──┘ │
//   prefix  device   tag  4 ASCII int     2-char ASCII hex CRC
//                        counts            (last field; preceded by ',')
//
// Frame-end marker: '\n' (CR stripped from buffer by the caller).
//
// Field layout:
//
//   buf[0..6]    "$AIRDAQ"
//   buf[7]       ','
//   buf[8..15]   device ID like "64.12.B8" (variable; not consumed)
//   buf[16]      ','
//   buf[17..19]  tag like "ADC" (variable; not consumed)
//   buf[20]      ','
//   buf[21..]    first ADC counts integer (Static)
//   ...          ',', 4 comma-separated ASCII decimal integers total
//   buf[len-3]   ','
//   buf[len-2..] 2-char ASCII hex CRC = sum of bytes [0..len-3) mod 256
//
// The parser is intentionally agnostic to:
//   - the exact prefix (only the leading '$' matters; '$AIRDAQ' on the
//     real wire, but the parser doesn't check the literal);
//   - the metadata field count between the prefix and the integers
//     (we anchor on byte 21 like the legacy parser, which assumed a
//     21-byte fixed header);
//   - the byte immediately preceding the CRC (',' on real $AIRDAQ wire;
//     earlier synth-frame prototypes used '*' — both work, neither byte
//     is parsed, the parser just steps over it).
//
// CRC algorithm: sum of unsigned bytes from buf[0] up to and including
// the byte before the separator (i.e., buf[0..len-4]), masked to 8
// bits. Compared against the two ASCII hex chars at buf[len-2..len-1].
// Caller can disable CRC enforcement.

#ifndef ONSPEED_CORE_BOOM_BOOM_PARSER_H
#define ONSPEED_CORE_BOOM_BOOM_PARSER_H

#include <cstddef>
#include <cstdint>

namespace onspeed::boom {

struct BoomFrame {
    int  staticCounts  = 0;
    int  dynamicCounts = 0;
    int  alphaCounts   = 0;
    int  betaCounts    = 0;
    bool valid         = false;
};

// Decode a complete boom line (CR/LF already stripped by the caller).
// `len` is the byte count of buf, which ends with the 2 ASCII hex CRC
// chars at buf[len-2..len-1].
//
// If `checkCrc` is true, the CRC must validate or the result is invalid.
// If false, the CRC bytes are ignored but the parser still consumes them
// (so it knows where the last field ends).
BoomFrame Decode(const char* buf, int len, bool checkCrc);

}   // namespace onspeed::boom

#endif
