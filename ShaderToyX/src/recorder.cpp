/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "recorder.h"
#include "audio.h"   /* AUDIO_SAMPLE_RATE */

#define REC_FPS          60
#define REC_AAC_BYTES_PS 16000  /* 128 kbit/s */

static IMFSinkWriter *g_writer       = NULL;
static DWORD          g_video_stream = 0;
static DWORD          g_audio_stream = 0;
static bool           g_active       = false;
static bool           g_has_audio    = false;
static bool           g_mf_started   = false;
static int            g_width        = 0;
static int            g_height       = 0;
static LONGLONG       g_audio_frames = 0;   /* PCM frames written so far */
static LONGLONG       g_video_end    = 0;   /* end ts of the last video frame */
static wchar_t        g_path[MAX_PATH];

/* Sound is generated seconds ahead of real time, so PCM is held here and
   only released up to the video clock — otherwise the audio track would
   outrun the last video frame by the whole generation lead at stop time. */
#define FIFO_CAP_FRAMES (AUDIO_SAMPLE_RATE * 8)
static short *g_pcm_fifo   = NULL;
static int    g_fifo_count = 0;   /* frames currently held */

static HRESULT write_sample(DWORD stream, const void *data, DWORD size,
                            LONGLONG ts, LONGLONG duration, int flip_height);

/* ------------------------------------------------------------------ */
/*  Output path: exe directory first, %USERPROFILE%\Videos second     */
/* ------------------------------------------------------------------ */
static bool build_path(wchar_t *out, size_t cap, int attempt)
{
    wchar_t dir[MAX_PATH];

    if (attempt == 0)
    {
        DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
        {
            return false;
        }
        wchar_t *slash = wcsrchr(dir, L'\\');
        if (!slash)
        {
            return false;
        }
        *slash = L'\0';
    }
    else
    {
        wchar_t profile[MAX_PATH];
        if (!GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH))
        {
            return false;
        }
        swprintf(dir, MAX_PATH, L"%s\\Videos", profile);
    }

    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (!lt)
    {
        return false;
    }
    swprintf(out, cap, L"%s\\ShaderToyX_%04d-%02d-%02d_%02d-%02d-%02d.mp4",
             dir, lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
             lt->tm_hour, lt->tm_min, lt->tm_sec);
    return true;
}

/* ------------------------------------------------------------------ */
static HRESULT add_streams(IMFSinkWriter *w, int width, int height, bool audio)
{
    HRESULT       hr;
    IMFMediaType *mt = NULL;

    /* Bitrate heuristic: ~0.1 bits per pixel at 60 fps, clamped */
    UINT32 bitrate = (UINT32)((long long)width * height * 6);
    if (bitrate <  4000000) bitrate =  4000000;
    if (bitrate > 20000000) bitrate = 20000000;

    /* ---- Video output: H.264 ---- */
    hr = MFCreateMediaType(&mt);
    if (FAILED(hr)) return hr;
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    mt->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    mt->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(mt, MF_MT_FRAME_SIZE, (UINT32)width, (UINT32)height);
    MFSetAttributeRatio(mt, MF_MT_FRAME_RATE, REC_FPS, 1);
    MFSetAttributeRatio(mt, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = w->AddStream(mt, &g_video_stream);
    mt->Release();
    if (FAILED(hr)) return hr;

    /* ---- Video input: top-down BGRA (RGB32) ---- */
    hr = MFCreateMediaType(&mt);
    if (FAILED(hr)) return hr;
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    mt->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    mt->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32)(width * 4));
    MFSetAttributeSize(mt, MF_MT_FRAME_SIZE, (UINT32)width, (UINT32)height);
    MFSetAttributeRatio(mt, MF_MT_FRAME_RATE, REC_FPS, 1);
    MFSetAttributeRatio(mt, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = w->SetInputMediaType(g_video_stream, mt, NULL);
    mt->Release();
    if (FAILED(hr)) return hr;

    if (!audio)
    {
        return S_OK;
    }

    /* ---- Audio output: AAC ---- */
    hr = MFCreateMediaType(&mt);
    if (FAILED(hr)) return hr;
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    mt->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLE_RATE);
    mt->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    mt->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, REC_AAC_BYTES_PS);
    hr = w->AddStream(mt, &g_audio_stream);
    mt->Release();
    if (FAILED(hr)) return hr;

    /* ---- Audio input: 16-bit stereo PCM ---- */
    hr = MFCreateMediaType(&mt);
    if (FAILED(hr)) return hr;
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    mt->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLE_RATE);
    mt->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    mt->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
    mt->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, AUDIO_SAMPLE_RATE * 4);
    hr = w->SetInputMediaType(g_audio_stream, mt, NULL);
    mt->Release();
    return hr;
}

/* ------------------------------------------------------------------ */
static void mf_teardown(void)
{
    if (g_writer)
    {
        g_writer->Release();
        g_writer = NULL;
    }
    if (g_mf_started)
    {
        MFShutdown();
        g_mf_started = false;
    }
    free(g_pcm_fifo);
    g_pcm_fifo   = NULL;
    g_fifo_count = 0;
    g_active     = false;
    g_has_audio  = false;
}

/* ------------------------------------------------------------------ */
bool recorder_start(int width, int height, bool with_audio)
{
    if (g_active || width < 2 || height < 2)
    {
        return false;
    }

    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
    {
        return false;
    }
    g_mf_started = true;

    IMFAttributes *attr = NULL;
    MFCreateAttributes(&attr, 2);
    if (attr)
    {
        /* Prefer the GPU's H.264 encoder; don't let the writer block the
           audio stream for running ahead of video (it does, by design) */
        attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        attr->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    }

    HRESULT hr = E_FAIL;
    for (int attempt = 0; attempt < 2 && FAILED(hr); attempt++)
    {
        if (build_path(g_path, MAX_PATH, attempt))
        {
            hr = MFCreateSinkWriterFromURL(g_path, NULL, attr, &g_writer);
        }
    }
    if (attr)
    {
        attr->Release();
    }
    if (FAILED(hr))
    {
        mf_teardown();
        return false;
    }

    hr = add_streams(g_writer, width, height, with_audio);
    if (SUCCEEDED(hr))
    {
        hr = g_writer->BeginWriting();
    }
    if (FAILED(hr))
    {
        mf_teardown();
        DeleteFileW(g_path); /* remove the empty stub file */
        return false;
    }

    if (with_audio)
    {
        g_pcm_fifo = (short *)malloc((size_t)FIFO_CAP_FRAMES * 4);
        if (!g_pcm_fifo)
        {
            mf_teardown();
            DeleteFileW(g_path);
            return false;
        }
    }

    g_active       = true;
    g_has_audio    = with_audio;
    g_width        = width;
    g_height       = height;
    g_audio_frames = 0;
    g_video_end    = 0;
    g_fifo_count   = 0;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Release buffered PCM up to the video clock; false on write error  */
/* ------------------------------------------------------------------ */
static bool drain_audio(void)
{
    while (g_fifo_count > 0)
    {
        LONGLONG want = g_video_end * AUDIO_SAMPLE_RATE / 10000000LL
                        - g_audio_frames;
        int writable = (want < g_fifo_count) ? (int)want : g_fifo_count;
        if (writable <= 0)
        {
            break;
        }

        LONGLONG ts       = g_audio_frames * 10000000LL / AUDIO_SAMPLE_RATE;
        LONGLONG duration = (LONGLONG)writable * 10000000LL / AUDIO_SAMPLE_RATE;
        HRESULT hr = write_sample(g_audio_stream, g_pcm_fifo,
                                  (DWORD)writable * 4, ts, duration, 0);
        if (FAILED(hr))
        {
            return false;
        }
        g_audio_frames += writable;
        g_fifo_count   -= writable;
        memmove(g_pcm_fifo, g_pcm_fifo + (size_t)writable * 2,
                (size_t)g_fifo_count * 4);
    }
    return true;
}

/* ------------------------------------------------------------------ */
void recorder_stop(void)
{
    if (!g_active)
    {
        return;
    }
    if (g_has_audio)
    {
        /* Emit audio up to the last video frame; the rest of the
           generated-ahead lead is discarded with the FIFO */
        drain_audio();
    }
    g_writer->Finalize();
    mf_teardown();
}

/* ------------------------------------------------------------------ */
bool recorder_is_active(void)
{
    return g_active;
}

/* ------------------------------------------------------------------ */
/*  Write one sample from a locked memory buffer; shared plumbing     */
/* ------------------------------------------------------------------ */
static HRESULT write_sample(DWORD stream, const void *data, DWORD size,
                            LONGLONG ts, LONGLONG duration, int flip_height)
{
    IMFMediaBuffer *buf = NULL;
    HRESULT hr = MFCreateMemoryBuffer(size, &buf);
    if (FAILED(hr))
    {
        return hr;
    }

    BYTE *dst = NULL;
    hr = buf->Lock(&dst, NULL, NULL);
    if (SUCCEEDED(hr))
    {
        if (flip_height > 0)
        {
            /* GL rows are bottom-up; MF wants top-down at positive stride */
            DWORD stride = size / (DWORD)flip_height;
            const BYTE *src = (const BYTE *)data;
            for (int y = 0; y < flip_height; y++)
            {
                memcpy(dst + (size_t)y * stride,
                       src + (size_t)(flip_height - 1 - y) * stride, stride);
            }
        }
        else
        {
            memcpy(dst, data, size);
        }
        buf->Unlock();
        buf->SetCurrentLength(size);
    }

    IMFSample *sample = NULL;
    if (SUCCEEDED(hr))
    {
        hr = MFCreateSample(&sample);
    }
    if (SUCCEEDED(hr))
    {
        sample->AddBuffer(buf);
        sample->SetSampleTime(ts);
        sample->SetSampleDuration(duration);
        hr = g_writer->WriteSample(stream, sample);
    }
    if (sample)
    {
        sample->Release();
    }
    buf->Release();
    return hr;
}

/* ------------------------------------------------------------------ */
void recorder_write_video(const unsigned char *bgra_bottom_up, double time_sec)
{
    if (!g_active)
    {
        return;
    }
    LONGLONG ts       = (LONGLONG)(time_sec * 10000000.0 + 0.5);
    LONGLONG duration = 10000000LL / REC_FPS;
    HRESULT hr = write_sample(g_video_stream,
                              bgra_bottom_up,
                              (DWORD)(g_width * g_height * 4),
                              ts, duration, g_height);
    if (FAILED(hr))
    {
        recorder_stop(); /* disk full, device lost, ... */
        return;
    }
    g_video_end = ts + duration;

    if (g_has_audio && !drain_audio())
    {
        recorder_stop();
    }
}

/* ------------------------------------------------------------------ */
void recorder_write_audio(const short *pcm, int frames)
{
    if (!g_active || !g_has_audio || frames <= 0)
    {
        return;
    }

    /* Queue only; drain_audio() releases it as the video clock advances.
       Overflow cannot happen in practice (the pending lead is bounded by
       the ~4 s WASAPI buffer); clip defensively if it ever does. */
    int room = FIFO_CAP_FRAMES - g_fifo_count;
    if (frames > room)
    {
        frames = room;
    }
    memcpy(g_pcm_fifo + (size_t)g_fifo_count * 2, pcm, (size_t)frames * 4);
    g_fifo_count += frames;
}
