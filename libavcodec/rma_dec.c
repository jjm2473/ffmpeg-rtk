/*
 * Rtk Media Acceleration Video decoder
 * Copyright (C) 2017 Realtek
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <pthread.h>

#include "config.h"

#include "libavutil/buffer_internal.h"
#include "libavutil/time.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/fifo.h"
#include "libavutil/avassert.h"

#include "avcodec.h"
#include "h264.h"
#include "internal.h"

#include "rtkMediaAccel.h"

#ifndef off64_t
#define off64_t __off64_t
#endif

#define RMA_LIBNAME "libRMA.so"

#define RTK_VERSION_MAJOR  1
#define RTK_VERSION_MINOR  3
#define RTK_VERSION_MICRO  3

typedef struct RMALibContext {
    void *lib;
    void *(*rma_Init)(const char *);
    RMA_ERRORTYPE (*rma_Uninit)(void*);
    RMA_ERRORTYPE (*rma_Start)(void*, RMA_PARAMETER);
    RMA_ERRORTYPE (*rma_FreeBuffer)(void *, void *);
    RMA_ERRORTYPE (*rma_EnqueueInputBuffer)(void*, unsigned char*, unsigned int, long long, RMA_ENQUEUETYPE);
    RMA_ERRORTYPE (*rma_DequeueOutputBuffer)(void*, unsigned int*, void*);
    RMA_ERRORTYPE (*rma_Flush)(void*);
} RMALibContext;



typedef struct RMADecContext {
    const AVClass *class;
    RMALibContext *rma_func;
    AVBSFContext *bsfc;
    AVCodecContext *avctx;
    void *rma_handler;
    const char *role;
    AVFifoBuffer *fifo;
    AVPacket filtered_pkt;

    int postinit_done;

    const char *libname;
    int dec_o_fps;
    int dec_o_width;
    int dec_o_height;
    int auto_resize;
    int turbo_mode;
    int rtk_version;
    int search_I_frm;
    int search_I_err_tolerance;
    int renderFlg;
    int dec_select;
    int draining;
    int eos;
} RMADecContext;


static const struct {
    enum AVPixelFormat lav;
    RMA_COLOR_FORMAT rma;
} rma_pix_fmt_map[] = {
    { AV_PIX_FMT_NV12,     RMA_COLOR_FORMAT_YUV420_SEMIPLANAR },
    { AV_PIX_FMT_YUV420P,  RMA_COLOR_FORMAT_YUV420_PLANAR },
    { AV_PIX_FMT_NONE,     RMA_COLOR_FORMAT_NONE },
};

static const struct {
    enum AVCodecID avId;
    RMA_CODEC rma;
    const char *role;
} rma_codec_map[] = {
#if CONFIG_MPEG4_RMA_DECODER
    { AV_CODEC_ID_MPEG4, RMA_CODEC_MPEG4, "video_decoder.mpeg4" },
#endif
#if CONFIG_H264_RMA_DECODER
    { AV_CODEC_ID_H264, RMA_CODEC_H264, "video_decoder.avc" },
#endif
#if CONFIG_HEVC_RMA_DECODER
    { AV_CODEC_ID_HEVC, RMA_CODEC_HEVC, "video_decoder.hevc" },
#endif
#if CONFIG_MPEG1_RMA_DECODER
    { AV_CODEC_ID_MPEG1VIDEO, RMA_CODEC_MPEG1, "video_decoder.mpeg2" },
#endif
#if CONFIG_MPEG2_RMA_DECODER
    { AV_CODEC_ID_MPEG2VIDEO, RMA_CODEC_MPEG2, "video_decoder.mpeg2" },
#endif
#if CONFIG_VP8_RMA_DECODER
    { AV_CODEC_ID_VP8, RMA_CODEC_VP8, "video_decoder.vp8" },
#endif
#if CONFIG_VP9_RMA_DECODER
    { AV_CODEC_ID_VP9, RMA_CODEC_VP9, "video_decoder.vp9" },
#endif
#if CONFIG_VC1_RMA_DECODER
    { AV_CODEC_ID_VC1, RMA_CODEC_VC1, "video_decoder.vc1" },
#endif
#if CONFIG_WMV3_RMA_DECODER
    { AV_CODEC_ID_WMV3, RMA_CODEC_WMV3, "video_decoder.wmv" },
#endif
#if CONFIG_MJPEG_RMA_DECODER
    { AV_CODEC_ID_MJPEG_RTK,  RMA_CODEC_MJPEG, "video_decoder.jpeg" },
#endif
#if CONFIG_H263_RMA_DECODER
    { AV_CODEC_ID_H263,  RMA_CODEC_H263,"video_decoder.h263" },
#endif
#if CONFIG_AVS_RMA_DECODER
    { AV_CODEC_ID_AVS,  RMA_CODEC_AVS,"video_decoder.avs" },
    { AV_CODEC_ID_CAVS,  RMA_CODEC_AVS,"video_decoder.avs" },
#endif
#if CONFIG_FLV_RMA_DECODER
    { AV_CODEC_ID_FLV1,  RMA_CODEC_FLV,"video_decoder.flv" },
#endif
    { AV_CODEC_ID_NONE,  RMA_CODEC_NONE, "video_decoder.none" },
};

static enum AVPixelFormat ff_rma_get_pix_fmt(RMA_COLOR_FORMAT rma);
static av_cold const char * ff_rma_get_role(enum AVCodecID avId);
static av_cold RMA_CODEC ff_rma_get_codec(enum AVCodecID avId);
static av_cold RMALibContext *rma_lib_init(void *logctx);


static enum AVPixelFormat ff_rma_get_pix_fmt(RMA_COLOR_FORMAT rma)
{
    unsigned i;
    for (i = 0; rma_pix_fmt_map[i].lav != AV_PIX_FMT_NONE; i++) {
        if (rma_pix_fmt_map[i].rma == rma)
            return rma_pix_fmt_map[i].lav;
    }
    return AV_PIX_FMT_NONE;
}

static av_cold const char * ff_rma_get_role(enum AVCodecID avId)
{
    unsigned i;
    for (i = 0; strcmp(rma_codec_map[i].role, "video_decoder.none") != 0; i++) {
        if (rma_codec_map[i].avId == avId)
            return rma_codec_map[i].role;
    }
    return "video_decoder.none";
}

static av_cold RMA_CODEC ff_rma_get_codec(enum AVCodecID avId)
{
    unsigned i;
    for (i = 0; rma_codec_map[i].rma != RMA_CODEC_NONE; i++) {
        if (rma_codec_map[i].avId == avId)
            return rma_codec_map[i].rma;
    }
    return RMA_CODEC_NONE;
}

static av_cold RMALibContext *rma_lib_init(void *logctx)
{
    RMALibContext *rma_func;

    rma_func = av_mallocz(sizeof(*rma_func));
    if (!rma_func)
        return NULL;

    rma_func->lib = dlopen(RMA_LIBNAME, RTLD_NOW | RTLD_GLOBAL);
    if (!rma_func->lib) {
        av_log(logctx, AV_LOG_WARNING, "%s not found\n", RMA_LIBNAME);
        av_free(rma_func);
        return NULL;
    }

    rma_func->rma_Init = dlsym(rma_func->lib, "RMA_Init");
    rma_func->rma_Uninit = dlsym(rma_func->lib, "RMA_Uninit");
    rma_func->rma_Start = dlsym(rma_func->lib, "RMA_Start");
    rma_func->rma_FreeBuffer = dlsym(rma_func->lib, "RMA_FreeBuffer");
    rma_func->rma_EnqueueInputBuffer = dlsym(rma_func->lib, "RMA_EnqueueInputBuffer");
    rma_func->rma_DequeueOutputBuffer = dlsym(rma_func->lib, "RMA_DequeueOutputBuffer");
    rma_func->rma_Flush = dlsym(rma_func->lib, "RMA_Flush");

    if (!rma_func->rma_Init || !rma_func->rma_Uninit || !rma_func->rma_Start || !rma_func->rma_FreeBuffer ||
        !rma_func->rma_EnqueueInputBuffer || !rma_func->rma_DequeueOutputBuffer || !rma_func->rma_Flush) {
        av_log(logctx, AV_LOG_WARNING, "Not all functions found in %s\n", RMA_LIBNAME);
        av_log(logctx, AV_LOG_WARNING, "%d, %d, %d, %d, %d, %d, %d\n", !rma_func->rma_Init, !rma_func->rma_Uninit,
            !rma_func->rma_Start, !rma_func->rma_FreeBuffer, !rma_func->rma_EnqueueInputBuffer,
            !rma_func->rma_DequeueOutputBuffer, !rma_func->rma_Flush);
        dlclose(rma_func->lib);
        rma_func->lib = NULL;
        av_free(rma_func);
        return NULL;
    }

    return rma_func;
}


static av_cold int rma_decode_init(AVCodecContext *avctx)
{
    RMADecContext *s = avctx->priv_data;
    int ret = AVERROR_DECODER_NOT_FOUND;
    const char *bsf_name = NULL;

    s->rma_func = rma_lib_init(avctx);
    if (!s->rma_func)
        return AVERROR_DECODER_NOT_FOUND;

    s->avctx = avctx;
    s->postinit_done = 0;

    if(avctx->opaque)
        s->renderFlg = *((int *)avctx->opaque);

    s->fifo = av_fifo_alloc(sizeof(AVPacket));
    if (!s->fifo) {
        ret = AVERROR(ENOMEM);
        return AVERROR_UNKNOWN;
    }

    av_init_packet(&s->filtered_pkt);

    s->role = ff_rma_get_role(avctx->codec->id);

    if(avctx->codec->id == AV_CODEC_ID_H264)
    {
        if(avctx->extradata && avctx->extradata[0] == 1)
            bsf_name = "h264_mp4toannexb";
    }
    else if(avctx->codec->id == AV_CODEC_ID_HEVC)
    {
        if(avctx->extradata && avctx->extradata[0] == 1)
            bsf_name = "hevc_mp4toannexb";
    }

    if (avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        //regarding ticks_per_frame description, should be 2 for h.264:
        avctx->ticks_per_frame = 2;
    }

    s->rma_handler = NULL;
    s->rma_handler = s->rma_func->rma_Init(s->role);
    s->libname = RMA_LIBNAME;
    s->draining = 0;
    s->eos = 0;
    if(!s->rma_handler)
    {
        av_free(s->rma_func);
        return AVERROR_UNKNOWN;
    }

    av_log(avctx, AV_LOG_INFO, "Using rtk %s\n", s->role);

    if (bsf_name) {
        const AVBitStreamFilter *bsf = av_bsf_get_by_name(bsf_name);
        if(!bsf)
            return AVERROR_BSF_NOT_FOUND;
        if ((ret = av_bsf_alloc(bsf, &s->bsfc)))
            return ret;
        if (((ret = avcodec_parameters_from_context(s->bsfc->par_in, avctx)) < 0) ||
            ((ret = av_bsf_init(s->bsfc)) < 0)) {
            av_bsf_free(&s->bsfc);
            return ret;
        }
    }
    return 0;
}


static void rma_decode_set_context_cb(void *opaque, RMA_COLOR_FORMAT colorFmt, unsigned int width, unsigned int height)
{
    AVCodecContext *avctx = opaque;
    if(colorFmt!=RMA_COLOR_FORMAT_NONE)
        avctx->pix_fmt = ff_rma_get_pix_fmt(colorFmt);

    if (width && height)
        ff_set_dimensions(avctx, width, height);
}

static int rma_decode_queue_input_buffer(AVCodecContext *avctx, const AVPacket* avpkt)
{
    RMADecContext *s = avctx->priv_data;
    int ret = 0;

    if (s->postinit_done == 0)
    {
        RMA_PARAMETER param = {0};

        if(s->rtk_version)
            av_log(avctx, AV_LOG_ERROR, "FFmpeg RTK Patch Version: %d.%d.%d\n", RTK_VERSION_MAJOR, RTK_VERSION_MINOR, RTK_VERSION_MICRO);

        param.codec = ff_rma_get_codec(avctx->codec_id);
        param.ori_width = avctx->width;
        param.ori_height = avctx->height;
        param.dec_o_width = s->dec_o_width;
        param.dec_o_height = s->dec_o_height;
        param.dec_o_fps = s->dec_o_fps;
        param.auto_resize = s->auto_resize;
        param.turbo_mode = s->turbo_mode;
        param.search_I_frm = s->search_I_frm;
        param.search_I_err_tolerance = s->search_I_err_tolerance;
        param.omx_version = s->rtk_version;
        param.renderFlg = s->renderFlg;
        param.dec_select = s->dec_select;
        param.pUserData = avctx;
        param.setParam = rma_decode_set_context_cb;

        s->rma_func->rma_Start(s->rma_handler, param);

        if (!s->bsfc)
        {
            if (avctx->extradata_size && avctx->extradata && avctx->codec_id != AV_CODEC_ID_MJPEG_RTK)
            {
                if(s->rma_func->rma_EnqueueInputBuffer(s->rma_handler, avctx->extradata, avctx->extradata_size, 0, RMA_ENQUEUE_CONFIG)<0)
                {
                    av_log(avctx, AV_LOG_ERROR, "RMA EnqueueInputBuffer fail %d\n", __LINE__);
                    return AVERROR_UNKNOWN;
                }
            }
        }

        s->postinit_done = 1;

    }


    if(s && avpkt && avpkt->size)
    {
        long long timeStamp = 0;
        int64_t pts = AV_NOPTS_VALUE;

        pts = (avpkt->pts != AV_NOPTS_VALUE) ? avpkt->pts : avpkt->dts;
        if (avctx->pkt_timebase.num && avctx->pkt_timebase.den) {
            timeStamp = av_rescale_q( pts, avctx->pkt_timebase, AV_TIME_BASE_Q);
        }
        else if (pts != AV_NOPTS_VALUE) {
            timeStamp = pts;
        }

        if(timeStamp < 0) //AV_NOPTS_VALUE
            timeStamp = 0;

        ret = s->rma_func->rma_EnqueueInputBuffer(s->rma_handler, avpkt->data, avpkt->size, timeStamp, RMA_ENQUEUE_FRAME);
        if(ret == RMA_ERR_AGAIN)
        {
            goto done;
        }
        else if(ret != RMA_SUCCESS)
        {
            av_log(avctx, AV_LOG_ERROR, "RMA EnqueueInputBuffer fail %d, ret %d\n", __LINE__, ret);
            goto done;
        }
    }
    else
    {
        s->draining = 1;

        ret = s->rma_func->rma_EnqueueInputBuffer(s->rma_handler, 0, 0, 0, RMA_ENQUEUE_EOS);
        if(ret == RMA_ERR_AGAIN)
        {
            goto done;
        }
        else if(ret != RMA_SUCCESS)
        {
            av_log(avctx, AV_LOG_ERROR, "RMA EnqueueInputBuffer fail %d\n", __LINE__);
            goto done;
        }
    }

done:
    return ret;
}


static void rma_free_buffer(void *opaque, uint8_t *data)
{
    RMADecContext *s = (RMADecContext *)opaque;
    RMA_BUFFERINFO *bufInfo = (RMA_BUFFERINFO *)data;

    if(s && s->rma_func && s->rma_handler && strcmp(s->libname, RMA_LIBNAME)==0)
    {
        s->rma_func->rma_FreeBuffer(s->rma_handler, bufInfo);
    }

    av_freep(&data);
}

static int rma_decode_dequeue_output_buffer(AVCodecContext *avctx, AVFrame* frame)
{
    RMADecContext *s = avctx->priv_data;
    int ret = 0;
    RMA_BUFFERINFO *bufInfo;
    unsigned int  outputFormatChange = 0;

    bufInfo = av_mallocz(sizeof(RMA_BUFFERINFO));

    ret = s->rma_func->rma_DequeueOutputBuffer(s->rma_handler, &outputFormatChange, bufInfo);

    if(ret == RMA_ERR_INSUFFICIENT_RESOURCE)
    {
        av_freep(&bufInfo);
        av_log(avctx, AV_LOG_DEBUG, "RMA EOF %d\n", __LINE__);
        return AVERROR_EOF;
    }

    if(ret == RMA_ERR_AGAIN)
    {
        av_freep(&bufInfo);
        return AVERROR(EAGAIN);
    }

    if ((ret = ff_decode_frame_props(avctx, frame)) < 0)
    {
        av_freep(&bufInfo);
        av_log(avctx, AV_LOG_ERROR, "RMA ff_decode_frame_props fail %d\n", __LINE__);
        return AVERROR(ENOMEM);
    }

    frame->width = avctx->width;
    frame->height = avctx->height;

    if (avctx->pkt_timebase.num && avctx->pkt_timebase.den) {
        frame->pts = av_rescale_q(bufInfo->nTimeStamp, AV_TIME_BASE_Q, avctx->pkt_timebase);
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_pts = av_rescale_q(bufInfo->nTimeStamp, AV_TIME_BASE_Q, avctx->pkt_timebase);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    } else {
        frame->pts = bufInfo->nTimeStamp;
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
        frame->pkt_pts = bufInfo->nTimeStamp;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }

    frame->pkt_dts = AV_NOPTS_VALUE;

    frame->buf[0] = av_buffer_create((uint8_t *)bufInfo, bufInfo->nAllocLen, rma_free_buffer, s, BUFFER_FLAG_READONLY);

    if (!frame->buf[0]) {
        av_log(avctx, AV_LOG_ERROR, "RMA frame->buf[0] is NULL %d\n", __LINE__);
        av_freep(&bufInfo);
        return AVERROR(ENOMEM);
    }

    frame->data[0] = bufInfo->pBuffer;

    frame->linesize[0] = bufInfo->stride;
    frame->data[1] = frame->data[0] + bufInfo->stride * bufInfo->plane_size;
    if (avctx->pix_fmt == AV_PIX_FMT_NV12) {
        frame->linesize[1] = bufInfo->stride;
    } else {
        // FIXME: assuming chroma plane's stride is 1/2 of luma plane's for YV12
        frame->linesize[1] = frame->linesize[2] = bufInfo->stride / 2;
        frame->data[2] = frame->data[1] + bufInfo->stride * bufInfo->plane_size / 4;
    }
    frame->rtk_data = bufInfo;

    return ret;
}


static int rma_dec_decode(AVCodecContext *avctx, RMADecContext *s,
                             AVFrame *frame, int *got_frame,
                             AVPacket *pkt)
{
    int ret;
    int length = pkt->size;

    ret = rma_decode_queue_input_buffer(avctx, pkt);
    if (ret == RMA_ERR_AGAIN) {
        length = 0;
    }
    else if (ret != RMA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR, "Failed to queue input buffer (status=%d)\n", ret);
        return AVERROR_UNKNOWN;
    }

    ret = rma_decode_dequeue_output_buffer(avctx, frame);
    if (ret== AVERROR_EOF){
        s->eos = 1;
        return 0;
    }
    if (ret < 0 && ret != AVERROR(EAGAIN))
        return ret;
    if (ret >= 0)
        *got_frame = 1;

    return length;
}

static int rma_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    RMADecContext *s = avctx->priv_data;
    AVFrame *frame    = data;
    int ret;

    /* buffer the input packet */
    if (avpkt->size) {
        AVPacket input_pkt = { 0 };

        if (av_fifo_space(s->fifo) < sizeof(input_pkt)) {
            ret = av_fifo_realloc2(s->fifo,
                                   av_fifo_size(s->fifo) + sizeof(input_pkt));
            if (ret < 0)
                return ret;
        }

        ret = av_packet_ref(&input_pkt, avpkt);
        if (ret < 0)
            return ret;
        av_fifo_generic_write(s->fifo, &input_pkt, sizeof(input_pkt), NULL);
    }

    while (!*got_frame) {
        if (s->filtered_pkt.size <= 0) {
            AVPacket input_pkt = { 0 };

            if(s->draining == 0)
                av_packet_unref(&s->filtered_pkt);

            /* no more data */
            if (av_fifo_size(s->fifo) < sizeof(AVPacket)) {
                if(avpkt->size) return avpkt->size;
                ret = rma_dec_decode(avctx, s, frame, got_frame, avpkt);
                if (ret < 0 || s->eos)
                    return ret;
                continue;
            }

            av_fifo_generic_read(s->fifo, &input_pkt, sizeof(input_pkt), NULL);

            if (s->bsfc) {
            ret = av_bsf_send_packet(s->bsfc, &input_pkt);
            if (ret < 0) {
                return ret;
            }

            ret = av_bsf_receive_packet(s->bsfc, &s->filtered_pkt);
            if (ret == AVERROR(EAGAIN)) {
                goto done;
            }
            } else {
                av_packet_move_ref(&s->filtered_pkt, &input_pkt);
            }

            /* {h264,hevc}_mp4toannexb are used here and do not require flushing */
            av_assert0(ret != AVERROR_EOF);

            if (ret < 0)
                return ret;
        }

        ret = rma_dec_decode(avctx, s, frame, got_frame, &s->filtered_pkt);
        if (ret < 0)
            return ret;

        s->filtered_pkt.size -= ret;
    }

done:
    return avpkt->size;

}

static av_cold int rma_decode_end(AVCodecContext *avctx)
{
    RMADecContext *s = avctx->priv_data;

    av_fifo_free(s->fifo);
    av_bsf_free(&s->bsfc);
    av_packet_unref(&s->filtered_pkt);

    if(s && s->rma_func)
    {
        if(s->rma_handler)
        {
            s->rma_func->rma_Uninit(s->rma_handler);
            s->rma_handler = NULL;
        }

        if(s->rma_func->lib)
        {
            dlclose(s->rma_func->lib);
            s->rma_func->lib = NULL;
            s->libname = NULL;
        }

        av_free(s->rma_func);
        s->rma_func = NULL;
    }

    return 0;
}

static void rma_decode_flush(AVCodecContext *avctx)
{
    RMADecContext *s = avctx->priv_data;

    while (av_fifo_size(s->fifo)) {
        AVPacket pkt;
        av_fifo_generic_read(s->fifo, &pkt, sizeof(pkt), NULL);
        av_packet_unref(&pkt);
    }
    av_fifo_reset(s->fifo);

    av_packet_unref(&s->filtered_pkt);

    s->rma_func->rma_Flush(s->rma_handler);
}

#define OFFSET(x) offsetof(RMADecContext, x)
#define VD  AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "dec_o_width", "set the output width to RMA decoder", OFFSET(dec_o_width), AV_OPT_TYPE_INT,{ .i64 = 0 } , 0, 1920, VD },
    { "dec_o_height", "set the output height to RMA decoder", OFFSET(dec_o_height), AV_OPT_TYPE_INT,{ .i64 = 0 } , 0, 1088, VD },
    { "dec_o_fps", "set the output fps to RMA decoder", OFFSET(dec_o_fps), AV_OPT_TYPE_INT,{ .i64 = 0 } , 0, 60, VD },
    { "auto_resize", "keeping the original width/height ratio", OFFSET(auto_resize), AV_OPT_TYPE_INT,{ .i64 = 0 } , 0, 1, VD },
    { "turbo_mode", "Speedup decode performance. Suggest enabling on 4k2k case", OFFSET(turbo_mode), AV_OPT_TYPE_INT,{ .i64 = 0 } , 0, 1, VD },
    { "rtk_version", "Show verion information about RTK patch and libs", OFFSET(rtk_version), AV_OPT_TYPE_INT,{ .i64 = 0 } , 0, 1, VD },
    { "search_I_frm", "Start to decode from first I frame", OFFSET(search_I_frm), AV_OPT_TYPE_INT,{ .i64 = 1 } , 0, 1, VD },
    { "search_I_err_tolerance", "The percentage of error MBs that an I frame can display(only valid when search_I_frm is 1, default is 3)", OFFSET(search_I_err_tolerance), AV_OPT_TYPE_INT,{ .i64 = 3 } , 0, 100, VD },
    { "dec_select", "Select which decoder(0 or 1) that you want", OFFSET(dec_select), AV_OPT_TYPE_INT, { 0 }, 0, 1, VD },
    { NULL }
};

#if CONFIG_H264_RMA_DECODER
static const AVClass rma_h264dec_class = {
    .class_name = "h264_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_rma_decoder = {
    .name             = "h264_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration H264 video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_H264,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_h264dec_class,
};
#endif

#if CONFIG_MPEG4_RMA_DECODER
static const AVClass rma_mpeg4dec_class = {
    .class_name = "mpeg4_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_mpeg4_rma_decoder = {
    .name             = "mpeg4_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration MPEG-4 video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_MPEG4,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_mpeg4dec_class,
};
#endif

#if CONFIG_HEVC_RMA_DECODER
static const AVClass rma_hevcdec_class = {
    .class_name = "hevc_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_hevc_rma_decoder = {
    .name             = "hevc_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration HEVC video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_HEVC,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_hevcdec_class,
};
#endif

#if CONFIG_MPEG2_RMA_DECODER
static const AVClass rma_mpeg2dec_class = {
    .class_name = "mpeg2_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_mpeg2_rma_decoder = {
    .name             = "mpeg2_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration MPEG-2 video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_MPEG2VIDEO,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_mpeg2dec_class,
};
#endif

#if CONFIG_MPEG1_RMA_DECODER
static const AVClass rma_mpeg1dec_class = {
    .class_name = "mpeg1_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_mpeg1_rma_decoder = {
    .name             = "mpeg1_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration MPEG-1 video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_MPEG1VIDEO,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_mpeg1dec_class,
};
#endif
#if CONFIG_VP8_RMA_DECODER
static const AVClass rma_vp8dec_class = {
    .class_name = "vp8_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_vp8_rma_decoder = {
    .name             = "vp8_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration VP8 video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_VP8,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_vp8dec_class,
};
#endif
#if CONFIG_VP9_RMA_DECODER
static const AVClass rma_vp9dec_class = {
    .class_name = "vp9_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_vp9_rma_decoder = {
    .name             = "vp9_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration VP9 video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_VP9,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_vp9dec_class,
};
#endif
#if CONFIG_VC1_RMA_DECODER
static const AVClass rma_vc1dec_class = {
    .class_name = "vc1_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_vc1_rma_decoder = {
    .name             = "vc1_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration VC1 video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_VC1,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_vc1dec_class,
};
#endif
#if CONFIG_WMV3_RMA_DECODER
static const AVClass rma_wmv3dec_class = {
    .class_name = "wmv3_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_wmv3_rma_decoder = {
    .name             = "wmv3_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration WMV3 video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_WMV3,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_wmv3dec_class,
};
#endif
#if CONFIG_MJPEG_RMA_DECODER
static const AVClass rma_mjpegdec_class = {
    .class_name = "mjpeg_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_mjpeg_rma_decoder = {
    .name             = "mjpeg_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration MJPEG video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_MJPEG_RTK,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_mjpegdec_class,
};
#endif
#if CONFIG_H263_RMA_DECODER
static const AVClass rma_h263dec_class = {
    .class_name = "h263_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h263_rma_decoder = {
    .name             = "h263_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration H.263 video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_H263,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_h263dec_class,
};
#endif
#if CONFIG_AVS_RMA_DECODER
static const AVClass rma_avsdec_class = {
    .class_name = "avs_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_avs_rma_decoder = {
    .name             = "avs_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration AVS (Audio Video Standard) video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_AVS,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_avsdec_class,
  };

static const AVClass rma_cavsdec_class = {
    .class_name = "cavs_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_cavs_rma_decoder = {
    .name             = "cavs_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration CAVS (Chinese Audio Video Standard) video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_CAVS,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS ,
    .priv_class       = &rma_cavsdec_class,
  };
#endif

#if CONFIG_FLV_RMA_DECODER
static const AVClass rma_flvdec_class = {
    .class_name = "flv_rma_decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_flv_rma_decoder = {
    .name             = "flv_rma",
    .long_name        = NULL_IF_CONFIG_SMALL("RTK Media Acceleration FLV (Sorenson Spark) video decoder"),
    .type             = AVMEDIA_TYPE_VIDEO,
    .id               = AV_CODEC_ID_FLV1,
    .priv_data_size   = sizeof(RMADecContext),
    .init             = rma_decode_init,
    .decode           = rma_decode_frame,
    .flush          = rma_decode_flush,
    .close            = rma_decode_end,
    .capabilities     = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,
    .caps_internal    = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP | FF_CODEC_CAP_SETS_PKT_DTS,
    .priv_class       = &rma_flvdec_class,
  };
#endif

