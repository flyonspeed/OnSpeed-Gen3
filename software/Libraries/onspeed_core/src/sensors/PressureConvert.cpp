// PressureConvert.cpp — raw HSC counts -> PSI -> IAS / Palt math
//
// All formulas are ported from sketch-side HscPressureSensor.cpp and
// SensorIO.cpp. Comments explain the derivation; the math is unchanged
// from the original to preserve bit-for-bit output during the extraction.

#include <sensors/PressureConvert.h>
#include <cmath>

namespace onspeed::sensors {

std::optional<float> CountsToPsi(uint16_t counts, HscRange range)
{
    // Reject counts outside the sensor's calibrated output window.
    // The HSC datasheet guarantees valid output only between the 10%
    // and 90% digital saturation points. Below or above that means the
    // sensor is pinned (disconnected, over-range, or powered off).
    if (counts < range.countsMin || counts > range.countsMax)
        return std::nullopt;

    // HSC linear transfer function per the HSC datasheet:
    //   PSI = (counts - countsMin) * (psiMax - psiMin)
    //         / (countsMax - countsMin) + psiMin
    //
    // This is the same linear interpolation as HscPressureSensor::ReadPressurePSI(uint16_t).
    const float span = static_cast<float>(range.countsMax - range.countsMin);
    return (static_cast<float>(counts) - static_cast<float>(range.countsMin))
           * (range.psiMax - range.psiMin)
           / span
           + range.psiMin;
}

float PitotPsiToIasKt(float dpPsi)
{
    if (dpPsi <= 0.0f)
        return 0.0f;

    // Convert PSI to Pascals.
    // 1 PSI = 6894.757 Pa = 68.94757 mbar * 100 Pa/mbar
    // This matches the existing SensorIO.cpp path:
    //   PfwdPascal = psi2mb(PfwdPSI) * 100
    // where psi2mb = 68.94757 (from OnSpeedTypes.h).
    const float dpPa = dpPsi * 6894.757f;

    // Incompressible pitot equation: IAS = sqrt(2 * dp / rho0)
    // rho0 = 1.225 kg/m^3 (ISA sea-level standard air density)
    // Result is m/s; multiply by 1.94384 to convert to knots.
    return sqrtf(2.0f * dpPa / 1.225f) * 1.94384f;
}

float StaticMbarToPaltFt(float staticMbar)
{
    if (staticMbar <= 0.0f)
        return 0.0f;

    // ISA barometric altitude formula (international standard atmosphere):
    //   Palt = 145366.45 * (1 - (P / P0)^0.190284)
    //
    // P0 = 1013.25 mbar (ISA sea-level standard pressure)
    // 0.190284 = 1/5.2561 (ISA troposphere pressure-altitude exponent)
    //
    // Ported verbatim from SensorIO.cpp::PressureAltitudeFeetFromMbar().
    // The bias correction (fPStaticBias) is a sketch-level concern and
    // is NOT applied here — the caller subtracts the bias before passing
    // the corrected pressure.
    return 145366.45f * (1.0f - powf(staticMbar / 1013.25f, 0.190284f));
}

float DensityAltitudeFt(float paltFt, float oatCelsius)
{
    // ISA standard temperature at pressure altitude (troposphere lapse rate):
    //   T_ISA = 15.0 - 0.001981 * paltFt   (°C)
    // Lapse rate: -6.5 K/km = -0.001981 K/ft (1 km = 3280.84 ft).
    const float isaOatC = 15.0f - 0.001981f * paltFt;

    // Standard aviation density altitude approximation:
    //   DA = PA + 120 * (OAT - ISA_OAT)
    //
    // 120 ft/°C is accurate to ~0.3% across the operating envelope of
    // light aircraft (sea level to FL180, -30 to +50°C).
    return paltFt + 120.0f * (oatCelsius - isaOatC);
}

}   // namespace onspeed::sensors
