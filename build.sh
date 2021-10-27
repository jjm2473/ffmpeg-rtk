#!/bin/bash

./configure \
	--pkg-config=pkg-config \
	--disable-avdevice \
	--disable-bzlib \
	--enable-debug \
	--disable-asm \
	--disable-stripping \
	--disable-optimizations \
	--disable-devices \
	--disable-doc \
	--disable-encoders \
	--disable-ffprobe \
	--disable-ffplay \
	--enable-pic \
	--enable-libmp3lame \
	--disable-hwaccels \
	--disable-iconv \
	--disable-lzma \
	--disable-protocol=concat \
	--disable-schannel \
	--disable-shared \
	--enable-static \
	--enable-gpl \
	--enable-libvorbis \
	--enable-muxers \
	--enable-version3 \
	--enable-omx \
	--enable-libass \
	--enable-rma \
	--enable-decoder=alac \
	--enable-decoder=ansi \
	--enable-decoder=apng \
	--enable-decoder=ass \
	--enable-decoder=ayuv \
	--enable-decoder=bmp \
	--enable-decoder=ccaption \
	--enable-decoder=dirac \
	--enable-decoder=dvbsub \
	--enable-decoder=dvdsub \
	--enable-decoder=ffv1 \
	--enable-decoder=ffvhuff \
	--enable-decoder=flac \
	--enable-decoder=gif \
	--enable-decoder=huffyuv \
	--enable-decoder=jacosub \
	--enable-decoder=libzvbi_teletext \
	--enable-decoder=microdvd \
	--enable-decoder=h264_rma \
	--enable-decoder=mpeg4_rma \
	--enable-decoder=hevc_rma \
	--enable-decoder=mpeg1_rma \
	--enable-decoder=mpeg2_rma \
	--enable-decoder=vp8_rma \
	--enable-decoder=vp9_rma \
	--enable-decoder=vc1_rma \
	--enable-decoder=wmv3_rma \
	--enable-decoder=mjpeg_rma \
	--enable-decoder=h263_rma \
	--enable-decoder=avs_rma \
	--enable-decoder=cavs_rma \
	--enable-decoder=flv_rma \
	--enable-decoder=mjpeg \
	--enable-decoder=movtext \
	--enable-decoder=mpl2 \
	--enable-decoder=opus \
	--enable-decoder=cook \
	--enable-decoder=wmapro \
	--enable-decoder=pcm_alaw \
	--enable-decoder=pcm_f32be \
	--enable-decoder=pcm_f32le \
	--enable-decoder=pcm_f64be \
	--enable-decoder=pcm_f64le \
	--enable-decoder=pcm_lxf \
	--enable-decoder=pcm_mulaw \
	--enable-decoder=pcm_s16be \
	--enable-decoder=pcm_s16be_planar \
	--enable-decoder=pcm_s16le \
	--enable-decoder=pcm_s16le_planar \
	--enable-decoder=pcm_s24be \
	--enable-decoder=pcm_s24le \
	--enable-decoder=pcm_s24le_planar \
	--enable-decoder=pcm_s32be \
	--enable-decoder=pcm_s32le \
	--enable-decoder=pcm_s32le_planar \
	--enable-decoder=pcm_s8 \
	--enable-decoder=pcm_s8_planar \
	--enable-decoder=pcm_u16be \
	--enable-decoder=pcm_u16le \
	--enable-decoder=pcm_u24be \
	--enable-decoder=pcm_u24le \
	--enable-decoder=pcm_u32be \
	--enable-decoder=pcm_u32le \
	--enable-decoder=pcm_u8 \
	--enable-decoder=pgssub \
	--enable-decoder=pjs \
	--enable-decoder=png \
	--enable-decoder=r210 \
	--enable-decoder=rawvideo \
	--enable-decoder=realtext \
	--enable-decoder=sami \
	--enable-decoder=ssa \
	--enable-decoder=stl \
	--enable-decoder=subrip \
	--enable-decoder=subviewer \
	--enable-decoder=text \
	--enable-decoder=thp \
	--enable-decoder=v210 \
	--enable-decoder=v210x \
	--enable-decoder=v308 \
	--enable-decoder=v408 \
	--enable-decoder=v410 \
	--enable-decoder=vorbis \
	--enable-decoder=vplayer \
	--enable-decoder=webvtt \
	--enable-decoder=xsub \
	--enable-decoder=y41p \
	--enable-decoder=yuv4 \
	--enable-decoder=zero12v \
	--enable-encoder=rawvideo \
	--enable-encoder=alac \
	--enable-encoder=ass \
	--enable-encoder=dvbsub \
	--enable-encoder=dvdsub \
	--enable-encoder=flac \
	--enable-encoder=h264_omx \
	--enable-encoder=libopus \
	--enable-encoder=libvorbis \
	--enable-encoder=mjpeg \
	--enable-encoder=movtext \
	--enable-encoder=pcm_f32be \
	--enable-encoder=pcm_f32le \
	--enable-encoder=pcm_f64be \
	--enable-encoder=pcm_f64le \
	--enable-encoder=pcm_s16be \
	--enable-encoder=pcm_s16be_planar \
	--enable-encoder=pcm_s16le \
	--enable-encoder=pcm_s16le_planar \
	--enable-encoder=pcm_s24be \
	--enable-encoder=pcm_s24le \
	--enable-encoder=pcm_s24le_planar \
	--enable-encoder=pcm_s32be \
	--enable-encoder=pcm_s32le \
	--enable-encoder=pcm_s32le_planar \
	--enable-encoder=pcm_s8 \
	--enable-encoder=pcm_s8_planar \
	--enable-encoder=pcm_u16be \
	--enable-encoder=pcm_u16le \
	--enable-encoder=pcm_u24be \
	--enable-encoder=pcm_u24le \
	--enable-encoder=pcm_u32be \
	--enable-encoder=pcm_u32le \
	--enable-encoder=pcm_u8 \
	--enable-encoder=ssa \
	--enable-encoder=subrip \
	--enable-encoder=text \
	--enable-encoder=webvtt \
	--enable-encoder=wrapped_avframe \
	--enable-encoder=xsub \
	--enable-decoder=pcm_dvd \
	--enable-decoder=eac3 \
	--enable-decoder=ac3 \
	--enable-encoder=mp2 \
	--enable-encoder=eac3 \
	--enable-encoder=aac \
	--enable-encoder=ac3 \
	--enable-decoder=aac_latm \
	--enable-nonfree \
	--enable-libfdk-aac --enable-encoder=libfdk_aac \
	--enable-libmp3lame --enable-encoder=libmp3lame \
	--enable-libopus --enable-decoder=libopus --enable-encoder=libopus
