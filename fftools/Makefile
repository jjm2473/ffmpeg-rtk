AVPROGS-$(CONFIG_FFMPEG)   += ffmpeg
AVPROGS-$(CONFIG_FFPLAY)   += ffplay
AVPROGS-$(CONFIG_FFPROBE)  += ffprobe
WRAPPROGS-$(CONFIG_RMA)   += ffmpeg_wrap

AVPROGS     := $(AVPROGS-yes:%=%$(PROGSSUF)$(EXESUF))
WRAPPROGS     := $(WRAPPROGS-yes:%=%$(PROGSSUF)$(EXESUF))
PROGS       += $(AVPROGS) $(WRAPPROGS)

AVBASENAMES  = ffmpeg ffplay ffprobe ffmpeg_wrap
ALLAVPROGS   = $(AVBASENAMES:%=%$(PROGSSUF)$(EXESUF))
ALLAVPROGS_G = $(AVBASENAMES:%=%$(PROGSSUF)_g$(EXESUF))

OBJS-ffmpeg                        += fftools/ffmpeg_opt.o fftools/ffmpeg_filter.o fftools/ffmpeg_hw.o
OBJS-ffmpeg-$(CONFIG_CUVID)        += fftools/ffmpeg_cuvid.o
OBJS-ffmpeg-$(CONFIG_LIBMFX)       += fftools/ffmpeg_qsv.o
ifndef CONFIG_VIDEOTOOLBOX
OBJS-ffmpeg-$(CONFIG_VDA)          += fftools/ffmpeg_videotoolbox.o
endif
OBJS-ffmpeg-$(CONFIG_VIDEOTOOLBOX) += fftools/ffmpeg_videotoolbox.o

define DOFFTOOL
OBJS-$(1) += fftools/cmdutils.o fftools/$(1).o $(OBJS-$(1)-yes)
$(1)$(PROGSSUF)_g$(EXESUF): $$(OBJS-$(1))
$$(OBJS-$(1)): | fftools
$$(OBJS-$(1)): CFLAGS  += $(CFLAGS-$(1))
$(1)$(PROGSSUF)_g$(EXESUF): LDFLAGS += $(LDFLAGS-$(1))
$(1)$(PROGSSUF)_g$(EXESUF): FF_EXTRALIBS += $(EXTRALIBS-$(1))
-include $$(OBJS-$(1):.o=.d)
endef

$(foreach P,$(AVPROGS-yes),$(eval $(call DOFFTOOL,$(P))))

define DOWRAP
OBJS-$(1) += fftools/$(1).o $(OBJS-$(1)-yes)
$(1)$(PROGSSUF)_g$(EXESUF): $$(OBJS-$(1))
$$(OBJS-$(1)): | fftools
$$(OBJS-$(1)): CPPFLAGS  += -DHAVE_AV_CONFIG_H
$(1)$(PROGSSUF)_g$(EXESUF): FF_EXTRALIBS :=
-include $$(OBJS-$(1):.o=.d)
endef
$(foreach P,$(WRAPPROGS-yes),$(eval $(call DOWRAP,$(P))))

all: $(AVPROGS) $(WRAPPROGS)

fftools/ffprobe.o fftools/cmdutils.o: libavutil/ffversion.h | fftools
OBJDIRS += fftools

ifdef AVPROGS
install: install-progs install-data
endif

install-progs-yes:
install-progs-$(CONFIG_SHARED): install-libs

install-progs: install-progs-yes $(AVPROGS) $(WRAPPROGS)
	$(Q)mkdir -p "$(BINDIR)"
	$(INSTALL) -c -m 755 $(AVPROGS) $(WRAPPROGS) "$(BINDIR)"

uninstall: uninstall-progs

uninstall-progs:
	$(RM) $(addprefix "$(BINDIR)/", $(ALLAVPROGS))

clean::
	$(RM) $(ALLAVPROGS) $(ALLAVPROGS_G) $(CLEANSUFFIXES:%=fftools/%)
