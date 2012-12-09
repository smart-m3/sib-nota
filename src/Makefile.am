
bin_PROGRAMS = sib-nota

# Compiler flags
sib_nota_CFLAGS  = -Wall -I$(top_srcdir) -I$(top_srcdir)/include
sib_nota_CFLAGS += @GNOME_CFLAGS@ @NOTA_CFLAGS@ @LIBSIB_CFLAGS@


# Linker flags
sib_nota_LDFLAGS = @GNOME_LIBS@ @NOTA_LIBS@  @LIBSIB_LIBS@ -lexpat
#sib_nota_LDFLAGS += @RM_LIBS@


AM_CFLAGS = $(test_sib_CFLAGS) -g

#STUBGEN = /usr/local/bin/nota-stubgen3.pl
STUBGEN = nota-stubgen3.pl

# Put these in alphabetical order so that they are easy to find.

sib_nota_SOURCES = \
	main.c \
	SibAccessNota_service.c  \
	SibAccessNota_service.h  \
	SibAccessNota_common.h \
	nota_rm_handler.c \
	kp_server.c \
	sib_handler.c \
	sib_maxfd.c 

if RM
sib_nota_SOURCES += \
	NoTA_System_ResourceMng_R01_01_user.c \
	NoTA_System_ResourceMng_R01_01_user.h \
	NoTA_System_ResourceMng_R01_01_common.h 
endif

BUILT_SOURCES = \
	SibAccessNota_service.h \
	SibAccessNota_service.c \
	SibAccessNota_common.h

if RM
BUILT_SOURCES += \
	NoTA_System_ResourceMng_R01_01_user.c \
	NoTA_System_ResourceMng_R01_01_user.h 
endif

SibAccessNota_service.h: SibAccessNota_service.c
	@true

SibAccessNota_service.c: sib.xml 
	$(STUBGEN) . sib.xml

if RM
NoTA_System_ResourceMng_R01_01_user.h: NoTA_System_ResourceMng_R01_01_user.c
	@true
NoTA_System_ResourceMng_R01_01_user.c: NoTA_System_ResourceMng_R01_01_v03.xml
	$(STUBGEN) . NoTA_System_ResourceMng_R01_01_v03.xml
endif
