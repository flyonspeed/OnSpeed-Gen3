# Audio Wiring

Getting OnSpeed audio into your headset is the most important wiring decision in the installation. There are several options depending on your audio panel and preferences.

## Audio Output

The OnSpeed controller outputs **stereo audio** via an I2S DAC. The signal level is suitable for line-level audio panel inputs or headset amplifier inputs.

## Wiring Options

| Option | Stereo? | Muted by Comm TX? | Wiring Complexity | Best For |
|--------|---------|-------------------|-------------------|----------|
| **A: Audio Panel Alert Input** | Mono | No | Medium | Garmin panels with alert/annunciator input |
| **B: Audio Panel Aux Input** | Depends on panel | Depends on panel | Medium | Most audio panels with aux/music jacks |
| **C: Audio Panel Music Input** | Yes (stereo) | Yes (silenced when transmitting) | Medium | Stereo + 3D audio; OK with muting during TX |
| **D: Direct Headset Splice** | Yes | No | High | Maximum control, no audio panel dependency |

---

### Option A: Audio Panel Alert Input

Many audio panels have a dedicated **alert or annunciator input** that plays through the headset regardless of the selected audio source. This is ideal for OnSpeed because:

- The alert always plays — it won't get accidentally switched off
- It doesn't compete with radio audio (typically mixes on top)
- It's not silenced when you transmit

**Drawback**: Alert inputs are usually **mono** — you won't get 3D audio panning.

**Wiring**: Connect OnSpeed audio left channel to the alert input signal line. Connect ground to audio ground.

### Option B: Audio Panel Aux Input

Most modern audio panels have at least one auxiliary input (sometimes labeled AUX, AUX1/AUX2, or similar). Connect OnSpeed audio to this input.

- Whether you get stereo depends on your panel model
- Whether audio mutes during transmit depends on your panel model and configuration
- You need to ensure the aux channel is selected/enabled on the panel

**Wiring**: Connect OnSpeed left and right audio channels to the aux input. Connect ground to audio ground. Consult your audio panel's installation manual for the specific connector pinout.

### Option C: Audio Panel Music Input

Many panels have a **music** or **entertainment** input (3.5mm jack or dedicated connector). This typically provides:

- **Stereo** — full left/right for 3D audio
- **Auto-mute during transmit** — the panel silences music when you key the mic

The auto-mute during transmit is usually acceptable because OnSpeed tones are most useful during approach and landing, not while talking on the radio.

!!! note "Music input may have volume control"
    Some panels have a separate volume control for the music input. Make sure it's turned up enough to hear the OnSpeed tones clearly.

**Wiring**: Connect OnSpeed left and right audio channels to the music input. This is often a standard 3.5mm stereo jack on the panel face or a connector on the back.

### Option D: Direct Headset Splice

If you don't have an audio panel, or want full control, you can splice OnSpeed audio directly into your headset wiring.

- **Full stereo** — wire left and right channels to the headset left and right
- **Never muted** — you hear the tones regardless of radio state
- **Higher complexity** — requires splitting headset wires and adding a summing network

**Wiring approach**:

1. Identify the headset audio signal wires (left and right, plus ground)
2. Add series resistors to both the existing audio source and OnSpeed output to create a passive summing mixer
3. Mix the signals together before the headset connector

Typical values: 1kΩ to 10kΩ series resistors on each source. Adjust to balance OnSpeed volume against radio/intercom volume.

!!! warning "Don't connect outputs directly together"
    Never connect two audio outputs directly without isolation resistors. This can damage the output stages. Always use a summing resistor network.

---

## Garmin Audio Panel Integration

### GMA 345

The GMA 345 has the following relevant inputs:

- **Music input** — 3.5mm stereo jack on the panel face. Provides stereo, muted during TX. This is the simplest option for GMA 345 installations.
- **Marker/Alert input** — on the rear connector. Mono, not muted during TX. Better for ensuring tones are always audible.

### GTR 200

The GTR 200 (comm radio with built-in intercom) has:

- **Music input** — 3.5mm jack. Stereo, muted during TX.
- Check the GTR 200 installation manual for rear connector aux input options.

### GMA 240 / GMA 340

Older Garmin panels. Check the installation manual for aux/marker input availability. Most have at least a marker beacon input that could be repurposed.

---

## Wiring Tips

- **Use shielded audio cable** — to prevent RF interference from radios and avionics. Connect the shield to ground at the OnSpeed end only (avoid ground loops).
- **Keep audio wires away from power wires** — especially away from strobe power supplies, alternator wiring, and RF antenna feedlines.
- **Verify ground** — OnSpeed audio ground must have a common reference with your audio panel ground. In most aircraft, this is the airframe ground bus.
- **Test before closing up panels** — connect everything temporarily and verify audio plays through your headset before permanently routing wires.
- **Label your wires** — you (or the next owner) will thank yourself later.

## Testing Audio After Installation

1. Power on the OnSpeed controller
2. Connect to the OnSpeed WiFi and open the web interface
3. Use the `AUDIOTEST` console command (via USB serial) to play a test tone sequence
4. Verify you hear tones through your headset at a reasonable volume
5. Adjust volume via the OnSpeed volume control (hardware pot or web interface)
6. If using stereo, verify left and right channels are correct (the 3D audio feature pans left/right based on lateral G)

If you hear nothing, see [Troubleshooting — No Audio](../troubleshooting/no-audio.md).
