/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#ifndef AUDIO_H
#define AUDIO_H

/*
 * audio.h  -  WASAPI playback for sound shaders.
 *
 * A shared-mode render stream (16-bit stereo PCM at AUDIO_SAMPLE_RATE;
 * the mixer resamples as needed). No threads: the main loop polls
 * audio_frames_writable() each frame and tops the device buffer up with
 * audio_submit(). All functions are safe to call when the device could
 * not be opened; they simply do nothing.
 */

#define AUDIO_SAMPLE_RATE 44100

bool audio_init(void);      /* open the default output device; false = no audio */
void audio_shutdown(void);

/* Frames of free space in the device buffer (0 when unavailable/full).
   If the device was invalidated (unplugged, default changed), this
   reopens the new default device and returns 0 for this frame. */
int  audio_frames_writable(void);

/* Queue interleaved L/R 16-bit frames; must not exceed the writable count */
void audio_submit(const short *interleaved, int frames);

void audio_set_paused(bool paused);  /* freeze / resume playback */
void audio_set_mute(bool mute);      /* session mute (shows in the volume mixer) */

/* Drop any queued-but-unplayed audio so newly generated samples are heard
   immediately; the stream position continues from what was last audible. */
void audio_flush(void);

/* Drop queued audio and rewind the stream position to sample 0 */
void audio_reset(void);

/* Next sample index to generate (feeds the iSampleOffset uniform) */
int  audio_next_sample(void);

#endif
