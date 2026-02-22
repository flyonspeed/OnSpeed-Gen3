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
3. As you reach climb speed, the tones **go silent** (you're fast enough to be in the silent zone)
4. This entire transition takes just a few seconds

On a normal takeoff, you hear a brief blip of tone during acceleration — then silence. This is normal.

## Cruise

- **Audio**: Silent
- **Status**: System is running, logging data, monitoring AOA

Silence during cruise is expected and correct. You're well above approach speed, so the AOA is below the L/Dmax threshold.

## Descent and Pattern Entry

As you slow down entering the traffic pattern:

1. **Downwind**: Typically silent (above L/Dmax speed)
2. **Abeam the numbers / power reduction**: You may start hearing the first low pulses as you decelerate
3. **Base turn**: Low pulsing tone — you're in the approach speed range. Bank angle increases your stall speed, which effectively increases your AOA. The tones compensate for this automatically.

## Final Approach

This is where OnSpeed shines:

1. **Fly to the solid tone** — when you hear a steady, non-pulsing low tone, you're at the ideal approach speed for your current weight and configuration
2. **If the pulse starts** (low tone pulsing), you're slightly fast — reduce power slightly
3. **If the tone goes high** (high-pitched pulsing), you're slow — add power
4. **Corrections should be small** — think of flying the tone like flying the glideslope: small, smooth corrections

!!! tip "Trust the tone in turns"
    In the base-to-final turn, the load factor increases your stall speed. The airspeed indicator shows a higher number is needed, but the AOA tone automatically compensates. If the tone stays solid through the turn, you're at the right AOA regardless of the bank angle.

## Landing

As you flare:

1. The tone transitions from solid to slow pulsing (you're decelerating below approach speed)
2. This is normal — you're trading speed for pitch in the flare
3. On touchdown, the tones will fade as airspeed drops below the mute threshold

## Shutdown

1. After landing and taxi to parking, the audio goes silent (below mute threshold)
2. Power off the aircraft
3. The controller shuts down — logging stops, WiFi AP disappears

## Go-Around

If you need to go around:

1. **Add full power** immediately
2. The tone will transition from whatever you were hearing toward silence as you accelerate
3. Pitch for climb attitude
4. The rapid transition from approach tones to silence confirms you're accelerating and building energy
