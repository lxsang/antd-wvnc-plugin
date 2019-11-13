AUTOMAKE_OPTIONS = foreign



# check for system
if LINUX
    AM_CPPFLAGS = -Wl,--no-as-needed
else
    AM_CPPFLAGS = -Wl,-undefined,dynamic_lookup
endif


AM_CPPFLAGS += -W  -Wall -g -std=c99 -fPIC

lib_LTLIBRARIES = wvnc.la
wvnc_la_LDFLAGS = -module -avoid-version -shared
wvnc_la_SOURCES =  wvnc.c

EXTRA_DIST = README.md