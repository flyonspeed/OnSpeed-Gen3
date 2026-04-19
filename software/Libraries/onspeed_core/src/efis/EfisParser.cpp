// EfisParser.cpp
//
// Dispatcher implementation.

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

std::optional<EfisFrame> EfisParser::TakeFrame()
{
    switch (type_)
    {
        case EfisType::DynonSkyview: return dynonSkyview_.TakeFrame();
        case EfisType::DynonD10:     return dynonD10_.TakeFrame();
        case EfisType::GarminG5:     return garminG5_.TakeFrame();
        case EfisType::GarminG3X:    return garminG3X_.TakeFrame();
        case EfisType::MglBinary:    return mglBinary_.TakeFrame();
        case EfisType::Vn300:        return vn300_.TakeFrame();
        case EfisType::None:         return std::nullopt;
    }
    return std::nullopt;
}

std::optional<Vn300Data> EfisParser::TakeVn300Data()
{
    if (type_ == EfisType::Vn300)
        return vn300_.TakeVn300Data();
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
