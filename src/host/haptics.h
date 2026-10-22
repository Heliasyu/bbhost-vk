// bbhost-vk: HD haptics and adaptive triggers for the DualSense and the
// DualSense Edge (docs/vk/PLAN.md, Phase X).
//
// HD haptics: plugged in by USB, either controller is also a 4-channel sound
// card - channels 1 and 2 its speaker, 3 and 4 the left and right voice-coil
// motors - and what plays on 3 and 4 is what the hands feel. The game is a
// PS4 title and knows only the DualShock 4's two rumble motors, so the
// waveform is made here from two things: the game's rumble (each motor a tone
// the actuators carry well) and the game's sound effects - the main audio
// port's 7.1 mix (music has a port of its own, hle/audio.cpp), its LFE and
// the low end of each side, with a transient detector so impacts, shots and
// footsteps come through and steady rain, wind and fire do not. Over
// Bluetooth, or with any other controller, there is no such device and the
// classic rumble runs (Haptics: Rumble).
//
// Adaptive triggers: SDL's DualSense effect report (SDL_SendGamepadEffect),
// USB or Bluetooth. L2, the gun, has a trigger break; R2, the heavy attack,
// a light resistance that turns into a rising hum while it is held to charge.
// Both relax in loading screens and bbhost's menus.
#pragma once

#include <cstddef>
#include <cstdint>

// The game's audio thread, every main-port buffer: 8-channel float frames
// (FL FR C LFE SL SR BL BR) at 48 kHz, already at the port's volume.
void haptics_game_audio(const float* frames, unsigned count, float gain);

// The game's pad thread (scePadSetVibration). True when HD haptics took it -
// the caller then leaves the controller's own rumble alone, which would turn
// the audio haptics off until the next report.
bool haptics_rumble(std::uint8_t small, std::uint8_t large);

// The Haptics setting as it stands: 0 Off, 1 Rumble, 2 HD.
int haptics_mode();

// A controller came or went (the window's pump thread): look for the
// haptics device again.
void haptics_devices_changed();

// Whether a USB vendor:product is a DualSense or DualSense Edge.
bool haptics_is_dualsense(std::uint16_t vendor, std::uint16_t product);

// The pump thread, every pad update, for one DualSense: what its triggers
// should do now, as the 11-byte effect blocks of the controller's output
// report. `l2` and `r2` are 0..1; `relaxed` is a loading screen or a menu.
// `id` keeps each controller's charge timing apart.
struct TriggerEffects {
    std::uint8_t left[11]{};
    std::uint8_t right[11]{};
};
TriggerEffects haptics_triggers(std::uint32_t id, float l2, float r2, bool relaxed);

// Before the audio subsystem shuts down: closes the haptics output.
void haptics_shutdown();

// Tests: the trigger effect blocks, as the controller's firmware reads them.
void haptics_trigger_off(std::uint8_t out[11]);
void haptics_trigger_feedback(std::uint8_t out[11], int start, int strength);
void haptics_trigger_weapon(std::uint8_t out[11], int start, int end, int strength);
void haptics_trigger_vibration(std::uint8_t out[11], int position, int amplitude, int frequency);
