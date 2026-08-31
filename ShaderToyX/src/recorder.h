/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#ifndef RECORDER_H
#define RECORDER_H

/*
 * recorder.h  -  MP4 recording via the Media Foundation sink writer.
 *
 * H.264 video (hardware-encoded where available) plus optional AAC
 * audio, muxed into an .mp4 next to the executable (falling back to
 * %USERPROFILE%\Videos if that directory is not writable). No threads:
 * the main loop feeds frames and PCM; the sink writer's own worker
 * queues do the encoding.
 *
 * Timestamps: video samples carry the caller's clock (seconds since
 * recording started, pause-aware); audio timestamps are derived from
 * the running count of samples written, starting at zero.
 */

bool recorder_start(int width, int height, bool with_audio);
void recorder_stop(void);
bool recorder_is_active(void);

/* One video frame: bottom-up BGRA (as glReadPixels(GL_BGRA) delivers),
   width*height*4 bytes at the size passed to recorder_start. */
void recorder_write_video(const unsigned char *bgra_bottom_up, double time_sec);

/* Interleaved stereo 16-bit PCM; ignored when started without audio */
void recorder_write_audio(const short *pcm, int frames);

#endif
