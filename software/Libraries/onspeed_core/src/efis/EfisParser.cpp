// EfisParser.cpp
//
// Dispatcher implementation. Per-byte routing goes through a function
// pointer set at ChangeType() — one indirect call on the hot path,
// no per-byte switch on type_.

#include <efis/EfisParser.h>

namespace onspeed::efis {

EfisParser::EfisParser(EfisType type)
    : type_(EfisType::None)
    , feedFn_(feedNone)
{
    ChangeType(type);
}

void EfisParser::feedNone(EfisParser&, uint8_t) {}

void EfisParser::feedDynonSkyview(EfisParser& self, uint8_t b)
{
    self.dynonSkyview_.FeedByte(b);
}

void EfisParser::feedDynonD10(EfisParser& self, uint8_t b)
{
    self.dynonD10_.FeedByte(b);
}

void EfisParser::feedGarminG5(EfisParser& self, uint8_t b)
{
    self.garminG5_.FeedByte(b);
}

void EfisParser::feedGarminG3X(EfisParser& self, uint8_t b)
{
    self.garminG3X_.FeedByte(b);
}

void EfisParser::feedMglBinary(EfisParser& self, uint8_t b)
{
    self.mglBinary_.FeedByte(b);
}

void EfisParser::feedVn300(EfisParser& self, uint8_t b)
{
    self.vn300_.FeedByte(b);
}

bool EfisParser::TryTakeFrame(EfisFrame& out)
{
    switch (type_)
    {
        case EfisType::DynonSkyview: return dynonSkyview_.TryTakeFrame(out);
        case EfisType::DynonD10:     return dynonD10_.TryTakeFrame(out);
        case EfisType::GarminG5:     return garminG5_.TryTakeFrame(out);
        case EfisType::GarminG3X:    return garminG3X_.TryTakeFrame(out);
        case EfisType::MglBinary:    return mglBinary_.TryTakeFrame(out);
        case EfisType::Vn300:        return vn300_.TryTakeFrame(out);
        case EfisType::None:         return false;
    }
    __builtin_unreachable();
}

bool EfisParser::TryTakeVn300Data(Vn300Data& out)
{
    if (type_ == EfisType::Vn300)
        return vn300_.TryTakeVn300Data(out);
    return false;
}

std::optional<EfisFrame> EfisParser::TakeFrame()
{
    EfisFrame f;
    if (TryTakeFrame(f))
        return f;
    return std::nullopt;
}

std::optional<Vn300Data> EfisParser::TakeVn300Data()
{
    Vn300Data d;
    if (TryTakeVn300Data(d))
        return d;
    return std::nullopt;
}

void EfisParser::ChangeType(EfisType type)
{
    type_ = type;
    dynonSkyview_.Reset();
    dynonD10_.Reset();
    garminG5_.Reset();
    garminG3X_.Reset();
    mglBinary_.Reset();
    vn300_.Reset();
    switch (type)
    {
        case EfisType::DynonSkyview: feedFn_ = feedDynonSkyview; break;
        case EfisType::DynonD10:     feedFn_ = feedDynonD10;     break;
        case EfisType::GarminG5:     feedFn_ = feedGarminG5;     break;
        case EfisType::GarminG3X:    feedFn_ = feedGarminG3X;    break;
        case EfisType::MglBinary:    feedFn_ = feedMglBinary;    break;
        case EfisType::Vn300:        feedFn_ = feedVn300;        break;
        case EfisType::None:         feedFn_ = feedNone;         break;
    }
}

}   // namespace onspeed::efis
