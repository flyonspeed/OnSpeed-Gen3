# Normal Operations

What to expect from OnSpeed during a typical flight.

## Startup

1. Power on the aircraft electrical system
2. The OnSpeed controller boots in ~5 seconds
3. You'll hear a brief **startup chime** confirming audio is working
4. The heartbeat LED starts blinking
5. WiFi AP becomes available (SSID: `OnSpeed`)

No tones play on the ground because IAS is below the mute threshold (default: 25 knots).

## Taxi

- **Audio**: Silent (below IAS mute threshold)
- **Status**: The system is running and logging, but produces no audio cues

If you hear tones during taxi, it means either:

- The mute-under-IAS threshold is set too low
- There's wind blowing into the pitot/AOA ports (normal on a windy day — tones will stop when you start moving into the wind)

## Takeoff

During the takeoff roll and climb:

1. As you accelerate past the mute threshold (~25 knots), audio becomes active
2. You'll briefly hear the **slow tones** as you accelerate through the approach speed range
3. As you reach climb speed, the tones **go silent** (AOA drops below L/D~MAX~)
4. This entire transition takes just a few seconds

On a normal takeoff, you hear a brief blip of tone during acceleration — then silence. This is normal.

### Best-Angle Climb (V~X~)

If you need maximum climb gradient (obstacle clearance), fly to the ONSPEED solid tone after takeoff. This corresponds to the AOA for best angle of climb. The tone gives you V~X~ performance without requiring you to know V~X~ as an airspeed — which changes with weight and density altitude.

### Best-Rate Climb (V~Y~)

Best rate of climb occurs near L/D~MAX~ — approximately at the boundary between silence and the first low pulse. Fly just into the low pulse region for approximate V~Y~ performance.

## Cruise

- **Audio**: Silent
- **Status**: System is running, logging data, monitoring AOA

Silence during cruise is expected. You're well above approach speed — the wing is working at a low fraction of its capability. If the Vno chime is enabled, you'll hear a periodic chime if you exceed your configured Vno speed.

## Descent and Pattern Entry

As you slow down entering the traffic pattern:

1. **Downwind**: Typically silent (above L/D~MAX~ speed)
2. **Abeam the numbers / power reduction**: You may start hearing the first low pulses as you decelerate — this is L/D~MAX~, confirming you're slowing into the approach range
3. **Base turn**: Low pulsing tone — you're between L/D~MAX~ and ONSPEED. The tone accounts for the increased load factor in the turn automatically.

## Final Approach

This is where OnSpeed shines:

1. **Fly to the solid tone** — when the pulsing stops and you hear a steady low tone, you're ONSPEED
2. **If the pulse starts** (low tone pulsing), you're slightly fast — reduce power slightly or allow the airplane to decelerate
3. **If the tone goes high** (high-pitched pulsing), effective power is negative — **push**: add power and/or reduce AOA
4. **Corrections should be small and smooth** — think of flying the tone like flying the glideslope: proportional, measured inputs

!!! tip "Trust the tone in turns"
    In the base-to-final turn, load factor increases stall speed. The airspeed indicator can't tell you how much margin you have at your current bank angle, but the AOA tone can. If the tone stays solid through the turn, you're aerodynamically balanced regardless of bank angle.

## Landing

As you flare:

1. The tone transitions from solid to low pulsing (you're decelerating below ONSPEED)
2. This is normal — you're trading kinetic energy for a pitch change in the flare
3. On touchdown, the tones will fade as airspeed drops below the mute threshold

## Shutdown

1. After landing and taxi to parking, the audio goes silent (below mute threshold)
2. Power off the aircraft
3. The controller shuts down — logging stops, WiFi AP disappears

## Go-Around

If you need to go around:

1. **Add full power** immediately
2. The tone will transition from whatever you were hearing toward silence as you accelerate
3. Pitch for climb attitude — don't over-rotate; let speed build
4. The rapid transition from approach tones to silence confirms you're accelerating and building energy margin
