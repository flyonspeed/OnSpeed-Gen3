// EfisParser.h
//
// Dispatcher — owns the active protocol parser and routes bytes to it.
//
// Instantiate with the configured EfisType enum value; thereafter call
// FeedByte() for each byte received from the UART. Call TakeFrame() after
// each FeedByte() to check whether a complete frame is available.
//
// For VN-300 the full raw dataset is also available via TakeVn300Data()
// (see Vn300.h for the Vn300Data struct).
//
// Usage:
//
//   EfisParser parser(EfisType::DynonSkyview);
//   for (uint8_t byte : uart_bytes) {
//       parser.FeedByte(byte);
//       if (auto frame = parser.TakeFrame())
//           process(*frame);
//   }
//
// Or the new copy-free API:
//
//   EfisParser parser(EfisType::DynonSkyview);
//   EfisFrame frame;
//   for (uint8_t byte : uart_bytes) {
//       parser.FeedByte(byte);
//       if (parser.TryTakeFrame(frame))
//           process(frame);
//   }
//
// Changing the type mid-stream (ChangeType()) resets all parser state.

#ifndef ONSPEED_CORE_EFIS_EFIS_PARSER_H
#define ONSPEED_CORE_EFIS_EFIS_PARSER_H

#include <cstdint>
#include <optional>
#include <types/EfisFrame.h>
#include <efis/DynonSkyview.h>
#include <efis/DynonD10.h>
#include <efis/GarminG5.h>
#include <efis/GarminG3X.h>
#include <efis/MglBinary.h>
#include <efis/Vn300.h>

namespace onspeed::efis {

// EfisType — mirrors the original EfisSerialIO::EnEfisType enum.
// Values are intentionally identical so that Config.cpp can assign them
// without a translation table.
enum class EfisType : uint8_t {
    None         = 0,
    Vn300        = 1,
    DynonSkyview = 2,
    DynonD10     = 3,
    GarminG5     = 4,
    GarminG3X    = 5,
    MglBinary    = 6,
};

// EfisParser — dispatcher that owns the active protocol parser.
//
// Per-byte dispatch goes through a function pointer set by ChangeType(),
// eliminating the per-byte switch on type_. Hot path is one indirect
// call instead of a load + compare + branch chain.
class EfisParser {
public:
    explicit EfisParser(EfisType type = EfisType::None);

    // Feed one byte from the UART to the active parser. One indirect call,
    // no per-byte switch on type_.
    void FeedByte(uint8_t b) {
        feedFn_(*this, b);
    }

    // Copy-free frame retrieval. Returns true and fills `out` when a
    // complete frame is ready; returns false otherwise. Each successful
    // call consumes the pending frame.
    bool TryTakeFrame(EfisFrame& out);

    // VN-300 only: copy-free extended-dataset retrieval.
    bool TryTakeVn300Data(Vn300Data& out);

    // Legacy optional-returning API. Implemented in terms of TryTakeFrame.
    std::optional<EfisFrame> TakeFrame();
    std::optional<Vn300Data> TakeVn300Data();

    // Switch to a different EFIS type, resetting all parser state.
    void ChangeType(EfisType type);

    EfisType ActiveType() const { return type_; }

private:
    // Per-protocol thunks. Each just forwards to its parser's FeedByte().
    // ChangeType() stores one of these in feedFn_; the hot path is one
    // indirect call rather than a switch on type_ for every UART byte.
    static void feedNone(EfisParser&, uint8_t);
    static void feedDynonSkyview(EfisParser&, uint8_t);
    static void feedDynonD10(EfisParser&, uint8_t);
    static void feedGarminG5(EfisParser&, uint8_t);
    static void feedGarminG3X(EfisParser&, uint8_t);
    static void feedMglBinary(EfisParser&, uint8_t);
    static void feedVn300(EfisParser&, uint8_t);

    EfisType           type_;
    void               (*feedFn_)(EfisParser&, uint8_t);

    DynonSkyviewParser dynonSkyview_;
    DynonD10Parser     dynonD10_;
    GarminG5Parser     garminG5_;
    GarminG3XParser    garminG3X_;
    MglBinaryParser    mglBinary_;
    Vn300Parser        vn300_;
};

}   // namespace onspeed::efis

#endif
