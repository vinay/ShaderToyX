/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <string.h>

#include "audio.h"

/* Device buffer length in 100-ns units (~4 seconds). The main loop keeps
   it topped up one sound block (~1.5 s) at a time, so playback survives
   multi-second UI stalls without underrunning. */
#define AUDIO_BUFFER_100NS 40000000LL

static IAudioClient       *g_client        = NULL;
static IAudioRenderClient *g_render        = NULL;
static ISimpleAudioVolume *g_volume        = NULL;
static UINT32              g_buffer_frames = 0;
static bool                g_com_ok        = false;  /* we own a CoInitialize */
static bool                g_device_ok     = false;
static bool                g_playing       = false;  /* stream is started */
static bool                g_paused        = false;
static bool                g_muted         = false;
static int                 g_next_sample   = 0;

/* ------------------------------------------------------------------ */
static void device_close(void)
{
    if (g_volume) { g_volume->Release(); g_volume = NULL; }
    if (g_render) { g_render->Release(); g_render = NULL; }
    if (g_client)
    {
        g_client->Stop();
        g_client->Release();
        g_client = NULL;
    }
    g_playing   = false;
    g_device_ok = false;
}

/* ------------------------------------------------------------------ */
static bool device_open(void)
{
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice           *device     = NULL;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void **)&enumerator);
    if (SUCCEEDED(hr))
    {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    }
    if (SUCCEEDED(hr))
    {
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                              (void **)&g_client);
    }
    if (SUCCEEDED(hr))
    {
        /* AUTOCONVERTPCM lets shared mode accept this format regardless of
           the device's mix format (the engine resamples; Windows 10+). */
        WAVEFORMATEX wf   = {};
        wf.wFormatTag     = WAVE_FORMAT_PCM;
        wf.nChannels      = 2;
        wf.nSamplesPerSec = AUDIO_SAMPLE_RATE;
        wf.wBitsPerSample = 16;
        wf.nBlockAlign    = 4;
        wf.nAvgBytesPerSec = AUDIO_SAMPLE_RATE * 4;

        hr = g_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                  AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                  AUDIO_BUFFER_100NS, 0, &wf, NULL);
    }
    if (SUCCEEDED(hr)) hr = g_client->GetBufferSize(&g_buffer_frames);
    if (SUCCEEDED(hr)) hr = g_client->GetService(__uuidof(IAudioRenderClient),
                                                 (void **)&g_render);
    if (SUCCEEDED(hr)) hr = g_client->GetService(__uuidof(ISimpleAudioVolume),
                                                 (void **)&g_volume);

    if (device)     device->Release();
    if (enumerator) enumerator->Release();

    if (FAILED(hr))
    {
        device_close();
        return false;
    }

    g_volume->SetMute(g_muted, NULL);
    g_device_ok = true;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Keep the stream started whenever not paused. With nothing queued,   */
/*  a started shared-mode stream just renders silence.                  */
/* ------------------------------------------------------------------ */
static void update_running_state(void)
{
    if (!g_device_ok)
    {
        return;
    }
    if (!g_paused && !g_playing)
    {
        if (SUCCEEDED(g_client->Start()))
        {
            g_playing = true;
        }
    }
    else if (g_paused && g_playing)
    {
        g_client->Stop();
        g_playing = false;
    }
}

/* ------------------------------------------------------------------ */
/*  The device went away (unplugged, or the default changed): reopen    */
/*  the new default and continue the stream from the same position.     */
/* ------------------------------------------------------------------ */
static void recover_device(void)
{
    device_close();
    if (device_open())
    {
        update_running_state();
    }
}

/* ------------------------------------------------------------------ */
bool audio_init(void)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        return false;
    }
    g_com_ok = SUCCEEDED(hr);

    if (!device_open())
    {
        return false;
    }
    update_running_state();
    return true;
}

/* ------------------------------------------------------------------ */
void audio_shutdown(void)
{
    device_close();
    if (g_com_ok)
    {
        CoUninitialize();
        g_com_ok = false;
    }
}

/* ------------------------------------------------------------------ */
int audio_frames_writable(void)
{
    if (!g_device_ok)
    {
        return 0;
    }

    UINT32 padding = 0;
    HRESULT hr = g_client->GetCurrentPadding(&padding);
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
    {
        recover_device();
        return 0; /* try again next frame */
    }
    if (FAILED(hr))
    {
        return 0;
    }
    return (int)(g_buffer_frames - padding);
}

/* ------------------------------------------------------------------ */
void audio_submit(const short *interleaved, int frames)
{
    if (!g_device_ok || frames <= 0)
    {
        return;
    }

    BYTE *dst = NULL;
    HRESULT hr = g_render->GetBuffer((UINT32)frames, &dst);
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
    {
        recover_device();
        return;
    }
    if (FAILED(hr))
    {
        return;
    }

    memcpy(dst, interleaved, (size_t)frames * 4);
    g_render->ReleaseBuffer((UINT32)frames, 0);
    g_next_sample += frames;
}

/* ------------------------------------------------------------------ */
void audio_set_paused(bool paused)
{
    g_paused = paused;
    update_running_state();
}

/* ------------------------------------------------------------------ */
void audio_set_mute(bool mute)
{
    if (mute == g_muted)
    {
        return;
    }
    g_muted = mute;
    if (g_volume)
    {
        g_volume->SetMute(mute, NULL);
    }
}

/* ------------------------------------------------------------------ */
void audio_flush(void)
{
    if (!g_device_ok)
    {
        return;
    }

    /* Rewind the generator position to what has actually been played,
       so the freshly generated audio carries on seamlessly from there. */
    UINT32 padding = 0;
    if (SUCCEEDED(g_client->GetCurrentPadding(&padding)))
    {
        g_next_sample -= (int)padding;
        if (g_next_sample < 0)
        {
            g_next_sample = 0;
        }
    }

    /* IAudioClient::Reset requires a stopped stream */
    if (g_playing)
    {
        g_client->Stop();
        g_playing = false;
    }
    g_client->Reset();
    update_running_state();
}

/* ------------------------------------------------------------------ */
void audio_reset(void)
{
    g_next_sample = 0;
    if (!g_device_ok)
    {
        return;
    }
    if (g_playing)
    {
        g_client->Stop();
        g_playing = false;
    }
    g_client->Reset();
    update_running_state();
}

/* ------------------------------------------------------------------ */
int audio_next_sample(void)
{
    return g_next_sample;
}
