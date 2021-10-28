#ifdef ZLIB_CONST
// ffmpeg make will go here
#include "config.h"

// file lock
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <time.h>
#include <errno.h>

#define CPU_RESID 0
#define RMA_RESID 1

// for ffmpeg build
#include "cmdutils.h"
const char program_name[] = "ffmpeg_rtk";
const int program_birth_year = 2021;
void show_help_default(const char *opt, const char *arg) {}

#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

#define ACTION_MASK 0x0FF00
#define ARG_MASK 0x00FF

typedef enum {
    CONTINUE=0x0000,
    DROP=0x0100,
    EXTEND=0x0200,
    BREAK=0x0400,
} ACTION;

const char* BARGS[]={"-profile:v", "-pix_fmt", "-preset", "-level", "-crf", "-x264opts:0", NULL};
const char* BFLAGS[]={NULL};

static int arg_filter (const char **arg) {
    for(const char** p=BARGS;*p != NULL;++p) {
        if (!strcmp(*p, *arg)) {
            return DROP|0x2;
        }
    }
    return CONTINUE;
}

static int flag_filter (const char **arg) {
    for(const char** p=BFLAGS;*p != NULL;++p) {
        if (!strcmp(*p, *arg)) {
            return DROP|0x1;
        }
    }
    return CONTINUE;
}

int target_h = -1;
int target_w = -1;
int target_r = -1;
int libx264_to_omx = 0;
int has_input = 0;
int skip_video = 0;
int image_dump = 0;
int h264_image_dump = 0;
int dump_attachment = 0;
int copy_video = 0;
int aac = 0;
int hls = 0;
int ext_c;
const char* ext_v[16];

static int conv(const char **arg) {
    int i,w,h;
    const char *p = NULL;
    if (!strcmp("-vf", *arg) || !strcmp("-filter_complex", *arg)) {
        if (!strcmp("-vf", *arg)) {
            if (!strncmp("scale=trunc(", arg[1], 12)) {
                p = arg[1];
            }
        } else {
            p = strstr(arg[1], "scale=trunc(");
        }

        if (p) {
            if (1 != sscanf(p, "scale=trunc(min(max(iw\\,ih*dar)\\,%d)/2)*2:trunc(ow/dar/2)*2", &w) &&
                1 != sscanf(p, "scale=trunc(min(max(iw,ih*dar),%d)/2)*2:trunc(ow/dar/2)*2", &w)) {
                w = 1920;
            }
        } else if (!strncmp("zscale=", arg[1], 7) && target_h == -1) {
            h = 1080;
            w = 1920;
        }
        if (w > 1920) {
            w = 1920;
        } else if (w < 256) {
            w = 256;
        } else {
            w = w / 2 * 2;
        }
        h = w * 9 / 32 * 2;
        target_h = h;
        target_w = w;
        return DROP|0x2;
    } else if (!strncmp("-codec:v:", *arg, 9)) {
        if (!strcmp("libx264", arg[1])) {
            libx264_to_omx = 1;
            ext_c = 2;
            ext_v[0] = *arg;
            ext_v[1] = "h264_omx";
            return EXTEND|DROP|0x2;
        } else if (!strcmp("h264", arg[1])) {
            h264_image_dump = 1;
        } else if (!strcmp("copy", arg[1])) {
            copy_video = 1;
        }
    } else if (!strncmp("-profile:v", *arg, 10)) {
        return DROP|0x2;
    } else if (!strcmp("-codec:a:0", *arg)) {
        if (!strcmp("aac", arg[1]) || !strcmp("libfdk_aac", arg[1])) {
            aac = 1;
            i = 0;
            ext_c = 0;
            if (hls) {
                ext_c = 2;
                ext_v[i++] = "-flags:a";
                ext_v[i++] = "-global_header";
            }
#if CONFIG_LIBFDK_AAC_ENCODER
            ext_c += 2;
            ext_v[i++] = "-codec:a:0";
            ext_v[i++] = "libfdk_aac";
            return EXTEND|DROP|0x2;
#else
            if (ext_c) {
                return EXTEND;
            }
#endif
        }
    } else if (has_input && !strcmp("-f", *arg)) {
        if (!strcmp("hls", arg[1])) {
            hls = 1;
            if (aac) {
                ext_c = 2;
                ext_v[0] = "-flags:a";
                ext_v[1] = "-global_header";
                return EXTEND;
            }
        }
    } else if (!strcmp("-maxrate", *arg)) {
        if (libx264_to_omx) {
            ext_c = 2;
            ext_v[0] = "-b:v";
            ext_v[1] = arg[1];
            return EXTEND|CONTINUE;
        }
    } else if (!strcmp("-r", *arg)) {
        target_r = atoi(arg[1]);
    } else if (!strcmp("-vframes", *arg)) {
        if (!strcmp("1", arg[1])) {
            image_dump = 1;
        }
    } else if (!strcmp("-i", *arg)) {
        has_input = 1;
    } else if (!strncmp("-dump_attachment:", *arg, 17)) {
        dump_attachment = 1;
        return BREAK;
    } else if (!strcmp("-vn", *arg)) {
        skip_video = 1;
    }
    return CONTINUE;
}

typedef int (*Filter)(const char **) ;

Filter filters[]={conv, arg_filter, flag_filter, NULL};

static char* bufprintf(const char* fmt, ...) {
    static char buf[512] = {0};
    static char *p = buf;
    int count;
    char *ret = p;
    va_list va;
    va_start(va, fmt);
    count = vsprintf(p, fmt, va);
    va_end(va);
    if (count > 0) {
        p += (count + 1);
    } else if (count < 0){
        return NULL;
    } else {
        *p = '\0';
    }
    return ret;
}

#define MAX_RMA_DEC_ARGC 20

static int conv_opts(int argc, char *argv[], char* nargv[]) {
    int nargc = MAX_RMA_DEC_ARGC+1;
    for (int i=1; i<argc; ++i) {
        Filter *f;
        for (f = filters; *f != NULL; ++f) {
            int r = (*f)(argv+i);
            if (BREAK == r) {
                return MAX_RMA_DEC_ARGC+1;
            }
            if (EXTEND == (r & EXTEND)) {
                for (int j=0; j<ext_c; ++j) {
                    nargv[nargc++] = ext_v[j];
                }
            }
            if (DROP == (r & DROP)) {
                i += (r & ARG_MASK) - 1;
                break;
            }
        }

        if (*f == NULL) {
            nargv[nargc++] = argv[i];
        }
    }
    return nargc;
}

#ifdef ZLIB_CONST

/** 
 * lock res, auto release after exit
 * return locked index, return -1 if interrupted (errno == EINTR) or out of memory 
 */
static int acquire_res0(int res, int max) {
    struct timespec delay;
    int i, ret;
    int *locks = (int *)malloc(sizeof(int) * max);
    if (NULL == locks)
        return -1;
    delay.tv_sec = 0;
    delay.tv_nsec = 100000000L;
    char rmalock[] = "/var/lock/rma.N.lock";
    char cpulock[] = "/var/lock/cpu.N.lock";
    char* lockpath = res?rmalock:cpulock;

    for (i=0;i<max;++i) {
        lockpath[14] = '0'+i;
        int fd = open(lockpath, O_RDONLY|O_CREAT, S_IRUSR|S_IRGRP);
        if (fd != -1)
            locks[i] = fd;
        else
            goto fail;
    }
    i = 0;
    while (-1 == flock(locks[i], LOCK_EX|LOCK_NB)) {
        if (EWOULDBLOCK != errno) {
            printf("lock failed: %s\n", strerror(errno));
            return -1;
        }
        ++i;
        if (max == i) {
            if (-1 == nanosleep(&delay, NULL)) {
                goto fail;
            }
            i = 0;
        }
    }

    free(locks);
    return i;
fail:
    free(locks);
    return -1;
}

static int acquire_res(int res) {
    static const int defmax[] = {3,2};
    static const char * const envkeys[] = {"RTK_RES_CPU", "RTK_RES_RMA"};
    int max = defmax[res];
    char* val = getenv(envkeys[res]);
    if (NULL != val) {
        max = atoi(val);
        if (max < 1 || max > 10) {
            max = defmax[res];
        }
    }
    return acquire_res0(res, max);
}

#endif

int main(int argc, char *argv[])
{
    char* nargv[128];
    int pargc = 0;
    int nargc = conv_opts(argc, argv, nargv);

    nargv[nargc] = NULL;

    if (!dump_attachment && has_input && !copy_video) {
        if (target_r != -1) {
            nargv[MAX_RMA_DEC_ARGC - (pargc++)] = bufprintf("%d", target_r);
            nargv[MAX_RMA_DEC_ARGC - (pargc++)] = "-dec_o_fps";
        }

        if (1) {
            nargv[MAX_RMA_DEC_ARGC - (pargc++)] = "1";
            nargv[MAX_RMA_DEC_ARGC - (pargc++)] = "–auto_resize";
            nargv[MAX_RMA_DEC_ARGC - (pargc++)] = bufprintf("%d", target_h >= 144 && target_h <= 1080 ? target_h : 1080);
            nargv[MAX_RMA_DEC_ARGC - (pargc++)] = "-dec_o_height";
            nargv[MAX_RMA_DEC_ARGC - (pargc++)] = bufprintf("%d", target_w > 0 && target_w <= 1920 ? target_w : 1920);
            nargv[MAX_RMA_DEC_ARGC - (pargc++)] = "-dec_o_width";

            if (NULL != getenv("RTK_RMA_XERROR"))
                nargv[MAX_RMA_DEC_ARGC - (pargc++)] = "-xerror";
        }
    }

    nargv[MAX_RMA_DEC_ARGC - pargc] = "ffmpeg";
#ifndef ZLIB_CONST
    printf("pass %d args\n", nargc - MAX_RMA_DEC_ARGC + pargc);
    for (char **p = nargv+(MAX_RMA_DEC_ARGC - pargc); *p != NULL; ++p) {
        printf("%s ", *p);
    }
    printf("\n");
    return 0;
#else
    if (dump_attachment) {
        return execvp("ffmpeg.img", argv);
    } else {
        if (!skip_video && -1 == acquire_res(h264_image_dump || copy_video ? CPU_RESID : RMA_RESID))
            return 1;
        return execvp("ffmpeg.rtk", nargv+(MAX_RMA_DEC_ARGC - pargc));
    }
#endif
}
