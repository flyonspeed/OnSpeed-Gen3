// Free_Fonts.h (host stub)
//
// The real Free_Fonts.h in GaugeWidgets/ expands to `&fonts::FreeMono9pt7b`
// etc., which live in M5GFX. The renderer code passes these to
// MockM5Canvas::setFont(const void*) which just records their pointer
// value. We give each font macro a unique opaque address so the coord
// hash is stable.

#ifndef RENDERTEST_FREE_FONTS_H
#define RENDERTEST_FREE_FONTS_H

// Each symbol is a distinct sentinel struct whose `id` field makes the
// font addressable in a deterministic way. MockM5Canvas::setFont reads
// ->id rather than the pointer value, so ASLR does not randomize the
// recorded hash.
namespace mock_fonts {
    struct FontTag { int id; };
    inline constexpr FontTag F1{1}, F2{2}, F3{3}, F4{4}, F5{5}, F6{6}, F7{7}, F8{8},
                             F9{9}, F10{10}, F11{11}, F12{12}, F13{13}, F14{14},
                             F15{15}, F16{16};
}

#define GFXFF 1

#define FSS9   (&mock_fonts::F1)
#define FSS12  (&mock_fonts::F2)
#define FSS18  (&mock_fonts::F3)
#define FSS24  (&mock_fonts::F4)

#define FSSB9  (&mock_fonts::F5)
#define FSSB12 (&mock_fonts::F6)
#define FSSB18 (&mock_fonts::F7)
#define FSSB24 (&mock_fonts::F8)

#define FM9    (&mock_fonts::F9)
#define FM12   (&mock_fonts::F10)
#define FM18   (&mock_fonts::F11)
#define FM24   (&mock_fonts::F12)

#define FMB9   (&mock_fonts::F13)
#define FMB12  (&mock_fonts::F14)
#define FMB18  (&mock_fonts::F15)
#define FMB24  (&mock_fonts::F16)

#endif // RENDERTEST_FREE_FONTS_H
