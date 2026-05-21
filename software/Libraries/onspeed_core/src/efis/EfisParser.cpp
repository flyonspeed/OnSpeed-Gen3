// EfisParser.cpp
//
// Dispatcher implementation. Per-byte routing is a switch on type_; type_
// is set once at boot (only changed via the web UI), so the branch is
// perfectly predicted after the first byte and lets the compiler inline
// the per-protocol FeedByte() bodies.

#include <efis/EfisParser.h>

namespace onspeed::efis {

EfisParser::EfisParser(EfisType type)
    : type_(type)
{
}

void EfisParser::FeedByte(uint8_t b)
{
    switch (type_)
    {
        case EfisType::DynonSkyview: dynonSkyview_.FeedByte(b); break;
        case EfisType::DynonD10:     dynonD10_.FeedByte(b);     break;
        case EfisType::GarminG5:     garminG5_.FeedByte(b);     break;
        case EfisType::GarminG3X:    garminG3X_.FeedByte(b);    break;
        case EfisType::MglBinary:    mglBinary_.FeedByte(b);    break;
        case EfisType::Vn300:        vn300_.FeedByte(b);        break;
        case EfisType::None:         break;
    }
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
}

}   // namespace onspeed::efis
