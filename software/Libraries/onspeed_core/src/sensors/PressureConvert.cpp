// PressureConvert.cpp — raw HSC counts -> PSI -> IAS / Palt math
//
// HSC transfer function and ISA altitude formulas are ported from
// sketch-side HscPressureSensor.cpp and SensorIO.cpp. PitotPsiToIasKt uses
// the ASTM/ICAO compressible-flow CAS definition; see the function comment
// for the derivation.

#include <sensors/PressureConvert.h>
#include <util/OnSpeedTypes.h>
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

    // ASTM / ICAO compressible-flow CAS:
    //   V = a0 * sqrt(5 * ((qc / P0 + 1)^(2/7) - 1))
    //
    // Inversion of the isentropic subsonic pitot relation
    //   qc / P0 = (1 + 0.2 * (V / a0)^2)^(7/2) - 1
    // for a perfect gas with gamma = 1.4. Matches the incompressible form
    // sqrt(2 * qc / rho0) to within a fraction of a knot below M 0.3, and
    // tracks the ASTM/ICAO calibrated-airspeed definition at higher Mach.
    //
    // P0 = 101325 Pa  (ISA sea-level static pressure)
    // a0 = 340.2941 m/s (ISA sea-level speed of sound)
    constexpr float kP0Pa  = 101325.0f;
    constexpr float kA0Mps = 340.2941f;

    // Convert PSI to Pa via psi2mb (= 68.94757 mbar/psi) * 100 Pa/mbar.
    const float qcPa    = onspeed::psi2mb(dpPsi) * 100.0f;
    const float bracket = powf(qcPa / kP0Pa + 1.0f, 2.0f / 7.0f) - 1.0f;
    return onspeed::mps2kts(kA0Mps * sqrtf(5.0f * bracket));
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
