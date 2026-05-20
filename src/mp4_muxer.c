/**
 * @file    mp4_muxer.c
 * @brief   H264 -> MP4 muxer implementation using libmp4v2
 *
 * Converts H264 Annex B elementary stream to MP4 container.
 * - Auto-extracts SPS/PPS into avcC box
 * - Converts NALs to AVCC format (4-byte length prefix)
 * - Marks IDR frames as sync samples
 */

#include "mp4_muxer.h"
#include <mp4v2/mp4v2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- internal constants ---- */
#define BUF_INIT_SIZE     (256 * 1024)
#define VIDEO_TIME_SCALE  90000

/* ---- muxer context ---- */
struct mp4_muxer_s {
    MP4FileHandle  hFile;
    MP4TrackId     trackId;

    uint32_t       video_time_scale;
    uint32_t       sample_duration;
    uint16_t       width;
    uint16_t       height;

    uint8_t*       sps_data;
    uint16_t       sps_len;
    uint8_t*       pps_data;
    uint16_t       pps_len;
    bool           sps_pps_set;

    uint8_t*       frame_buffer;
    uint32_t       frame_buf_size;
    uint32_t       frame_buf_used;

    uint32_t       frame_count;
};

/* ---- helpers ---- */

static inline uint8_t nal_type(const uint8_t* hdr) { return hdr[0] & 0x1F; }

static int buf_grow(uint8_t** b, uint32_t* cap, uint32_t need) {
    if (need <= *cap) return 0;
    uint32_t n = *cap * 2;
    while (n < need) n *= 2;
    uint8_t* nb = (uint8_t*)realloc(*b, n);
    if (!nb) return -1;
    *b = nb; *cap = n;
    return 0;
}

static void write_be32(uint8_t* b, uint32_t v) {
    b[0]=(v>>24)&0xFF; b[1]=(v>>16)&0xFF; b[2]=(v>>8)&0xFF; b[3]=v&0xFF;
}

/* ---- public API ---- */

mp4_muxer_t* mp4_muxer_create(const char* fn, uint16_t w, uint16_t h, uint8_t fps) {
    if (!fn || !w || !h || !fps) return NULL;

    mp4_muxer_t* c = (mp4_muxer_t*)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->frame_buf_size = BUF_INIT_SIZE;
    c->frame_buffer = (uint8_t*)malloc(c->frame_buf_size);
    if (!c->frame_buffer) { free(c); return NULL; }

    c->video_time_scale = VIDEO_TIME_SCALE;
    c->sample_duration  = VIDEO_TIME_SCALE / fps;
    c->width = w; c->height = h;

    c->hFile = MP4Create(fn, 0);
    if (c->hFile == MP4_INVALID_FILE_HANDLE) {
        fprintf(stderr, "[muxer] MP4Create failed: %s\n", fn);
        free(c->frame_buffer); free(c); return NULL;
    }
    MP4SetTimeScale(c->hFile, c->video_time_scale);
    printf("[muxer] created %s, %dx%d@%dfps dur=%u\n", fn, w, h, fps, c->sample_duration);
    return c;
}

int mp4_muxer_write_frame(mp4_muxer_t* c, const uint8_t* data, uint32_t len, bool key) {
    if (!c || !data || !len) return -1;

    c->frame_buf_used = 0;
    const uint8_t* p = data, *end = data + len;

    while (p < end) {
        /* detect start code without skipping zeros */
        int sc = 0;
        if (p+4 <= end && p[0]==0 && p[1]==0 && p[2]==0 && p[3]==1) sc = 4;
        else if (p+3 <= end && p[0]==0 && p[1]==0 && p[2]==1) sc = 3;
        else { p++; continue; }

        const uint8_t* nal = p + sc;
        if (nal >= end) break;
        uint8_t type = nal_type(nal);
        
        /* find nal end (next start code) */
        const uint8_t* probe = nal + 1;
        const uint8_t* nal_end = end;
        while (probe + 3 <= end) {
            if (probe[0]==0 && probe[1]==0 && probe[2]==0 && probe[3]==1) { nal_end = probe; break; }
            if (probe[0]==0 && probe[1]==0 && probe[2]==1) { nal_end = probe; break; }
            probe++;
        }
        uint32_t nal_len = nal_end - nal;

        if (type == 7 /*SPS*/) {
            free(c->sps_data);
            c->sps_data = (uint8_t*)malloc(nal_len);
            memcpy(c->sps_data, nal, nal_len);
            c->sps_len = nal_len;
            c->sps_pps_set = false;
        } else if (type == 8 /*PPS*/) {
            free(c->pps_data);
            c->pps_data = (uint8_t*)malloc(nal_len);
            memcpy(c->pps_data, nal, nal_len);
            c->pps_len = nal_len;
            c->sps_pps_set = false;
        } else if (type == 6 /*SEI*/) {
            /* skip */
        } else {
            /* VCL + AUD: write in AVCC format */
            if (buf_grow(&c->frame_buffer, &c->frame_buf_size, c->frame_buf_used + 4 + nal_len))
                return -1;
            write_be32(c->frame_buffer + c->frame_buf_used, nal_len);
            c->frame_buf_used += 4;
            memcpy(c->frame_buffer + c->frame_buf_used, nal, nal_len);
            c->frame_buf_used += nal_len;
        }

        p = nal_end;
    }

    /* create track when SPS+PPS first available */
    if (!c->trackId && c->sps_data && c->pps_data) {
        uint8_t prof = c->sps_data[1];
        uint8_t comp = c->sps_data[2];
        uint8_t lev  = c->sps_data[3];

        c->trackId = MP4AddH264VideoTrack(c->hFile, c->video_time_scale, c->sample_duration,
                                          c->width, c->height, prof, comp, lev, 3);
        if (c->trackId == MP4_INVALID_TRACK_ID) {
            fprintf(stderr, "[muxer] AddH264VideoTrack failed\n");
            return -1;
        }
        MP4AddH264SequenceParameterSet(c->hFile, c->trackId, c->sps_data, c->sps_len);
        MP4AddH264PictureParameterSet(c->hFile, c->trackId, c->pps_data, c->pps_len);
        c->sps_pps_set = true;
        printf("[muxer] track=%u profile=0x%02X level=0x%02X\n", c->trackId, prof, lev);
    }
    /* if SPS/PPS changed after track creation, update them */
    else if (c->trackId && !c->sps_pps_set && c->sps_data && c->pps_data) {
        MP4AddH264SequenceParameterSet(c->hFile, c->trackId, c->sps_data, c->sps_len);
        MP4AddH264PictureParameterSet(c->hFile, c->trackId, c->pps_data, c->pps_len);
        c->sps_pps_set = true;
    }

    /* write sample */
    if (c->frame_buf_used > 0 && c->sps_pps_set) {
        if (!MP4WriteSample(c->hFile, c->trackId, c->frame_buffer, c->frame_buf_used,
                            c->sample_duration, 0, key)) {
            fprintf(stderr, "[muxer] WriteSample failed frame=%u\n", c->frame_count);
            return -1;
        }
        c->frame_count++;
    }

    return 0;
}

int mp4_muxer_write_nal(mp4_muxer_t* c, const uint8_t* nal, uint32_t len, uint8_t type) {
    if (!c || !nal || !len) return -1;
    if (type == 7) {
        free(c->sps_data); c->sps_data = (uint8_t*)malloc(len);
        memcpy(c->sps_data, nal, len); c->sps_len = len; c->sps_pps_set = false;
        return 0;
    }
    if (type == 8) {
        free(c->pps_data); c->pps_data = (uint8_t*)malloc(len);
        memcpy(c->pps_data, nal, len); c->pps_len = len; c->sps_pps_set = false;
        return 0;
    }
    if (type == 6) return 0;

    if (buf_grow(&c->frame_buffer, &c->frame_buf_size, c->frame_buf_used + 4 + len))
        return -1;
    write_be32(c->frame_buffer + c->frame_buf_used, len);
    c->frame_buf_used += 4;
    memcpy(c->frame_buffer + c->frame_buf_used, nal, len);
    c->frame_buf_used += len;
    return 0;
}

int mp4_muxer_flush_frame(mp4_muxer_t* c, bool key) {
    if (!c) return -1;
    if (!c->trackId && c->sps_data && c->pps_data) {
        uint8_t prof = c->sps_data[1], comp = c->sps_data[2], lev = c->sps_data[3];
        c->trackId = MP4AddH264VideoTrack(c->hFile, c->video_time_scale, c->sample_duration,
                                          c->width, c->height, prof, comp, lev, 3);
        if (c->trackId == MP4_INVALID_TRACK_ID) return -1;
        MP4AddH264SequenceParameterSet(c->hFile, c->trackId, c->sps_data, c->sps_len);
        MP4AddH264PictureParameterSet(c->hFile, c->trackId, c->pps_data, c->pps_len);
        c->sps_pps_set = true;
    }
    if (c->frame_buf_used > 0 && c->sps_pps_set) {
        if (!MP4WriteSample(c->hFile, c->trackId, c->frame_buffer, c->frame_buf_used,
                            c->sample_duration, 0, key)) return -1;
        c->frame_count++;
        c->frame_buf_used = 0;
    }
    return 0;
}

void mp4_muxer_close(mp4_muxer_t* c) {
    if (!c) return;
    printf("[muxer] close: frames=%u\n", c->frame_count);
    if (c->hFile != MP4_INVALID_FILE_HANDLE) MP4Close(c->hFile, 0);
    free(c->sps_data); free(c->pps_data); free(c->frame_buffer);
    free(c);
}
