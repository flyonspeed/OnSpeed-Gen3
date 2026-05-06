// SimConnect spike — proves the bridge's SimConnect surface against a
// running P3D / FSX / MSFS instance.  Throwaway.  Read README.md first.

#include <cstdio>
#include <cstdint>
#include <chrono>
#include <thread>
#include <atomic>

#include <windows.h>
#include <SimConnect.h>

namespace {

enum DataDefinitionId : SIMCONNECT_DATA_DEFINITION_ID
{
    DEF_FLIGHT_STATE = 1,
};

enum DataRequestId : SIMCONNECT_DATA_REQUEST_ID
{
    REQ_FLIGHT_STATE = 1,
};

#pragma pack(push, 1)
struct FlightState
{
    double alphaDeg;     // INCIDENCE ALPHA (deg)
    double iasKt;        // AIRSPEED INDICATED (kt)
    double accelBodyX;   // ACCELERATION BODY X (ft/s²) — body-frame, +right
    double pausedFlag;   // SIM PAUSED (0/1)
    double onGroundFlag; // SIM ON GROUND (0/1)
};
#pragma pack(pop)

std::atomic<bool> g_quit{false};

void CALLBACK Dispatch(SIMCONNECT_RECV* pData, DWORD /*cbData*/, void* /*ctx*/)
{
    if (pData->dwID != SIMCONNECT_RECV_ID_SIMOBJECT_DATA)
        return;

    auto* obj = static_cast<SIMCONNECT_RECV_SIMOBJECT_DATA*>(pData);
    if (obj->dwRequestID != REQ_FLIGHT_STATE)
        return;

    const auto* s = reinterpret_cast<const FlightState*>(&obj->dwData);

    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    const double t = duration<double>(steady_clock::now() - t0).count();

    // Convert ft/s² to G for human-friendly output.
    constexpr double kFt_S2_per_G = 32.174;
    const double latG = s->accelBodyX / kFt_S2_per_G;

    std::printf("t=%6.2fs  alpha=%6.2fdeg  ias=%5.1fkt  latG=%+5.2f  paused=%d  onGround=%d\n",
                t, s->alphaDeg, s->iasKt, latG,
                s->pausedFlag != 0.0 ? 1 : 0,
                s->onGroundFlag != 0.0 ? 1 : 0);
}

BOOL WINAPI ConsoleHandler(DWORD ev)
{
    if (ev == CTRL_C_EVENT || ev == CTRL_CLOSE_EVENT)
    {
        g_quit = true;
        return TRUE;
    }
    return FALSE;
}

} // namespace

int main()
{
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    HANDLE sc = nullptr;
    if (FAILED(SimConnect_Open(&sc, "OnSpeed-SimConnect-Spike",
                               nullptr, 0, nullptr, 0)))
    {
        std::fprintf(stderr,
            "SimConnect_Open failed.  Is P3D / MSFS running?  "
            "Default config talks to localhost:500 only.\n");
        return 1;
    }

    SimConnect_AddToDataDefinition(sc, DEF_FLIGHT_STATE,
        "INCIDENCE ALPHA", "Degrees");
    SimConnect_AddToDataDefinition(sc, DEF_FLIGHT_STATE,
        "AIRSPEED INDICATED", "Knots");
    SimConnect_AddToDataDefinition(sc, DEF_FLIGHT_STATE,
        "ACCELERATION BODY X", "Feet per second squared");
    SimConnect_AddToDataDefinition(sc, DEF_FLIGHT_STATE,
        "SIM PAUSED", "Bool");
    SimConnect_AddToDataDefinition(sc, DEF_FLIGHT_STATE,
        "SIM ON GROUND", "Bool");

    // Period SECOND with dwInterval=1 fires once per second.  We want 20 Hz,
    // so use SIMCONNECT_PERIOD_SIM_FRAME with downsample.  Actually:
    // SIMCONNECT_PERIOD_SIM_FRAME fires every sim tick (~30 Hz default).
    // For a tighter 20 Hz cadence we'd use SIMCONNECT_PERIOD_VISUAL_FRAME
    // and rate-limit on our side, OR fire every sim frame and let our tick
    // loop ignore extras.  For the spike, sim-frame is fine — we just want
    // to see live values stream.
    SimConnect_RequestDataOnSimObject(sc, REQ_FLIGHT_STATE, DEF_FLIGHT_STATE,
        SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD_SIM_FRAME,
        SIMCONNECT_DATA_REQUEST_FLAG_CHANGED);

    std::printf("Connected.  Streaming flight state.  Ctrl+C to exit.\n");

    while (!g_quit)
    {
        SimConnect_CallDispatch(sc, Dispatch, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    SimConnect_Close(sc);
    std::printf("Disconnected.\n");
    return 0;
}
