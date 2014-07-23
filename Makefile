PREFIX = /usr/local

BINFILES = tascar_renderer tascar_gui tascar_audioplayer		\
tascar_sampler tascar_osc_jack_transport tascar_oscmix tascar_jackio	\
tascar_pdf tascar_osc_recorder tascar_creator tascar_ambwarping		\
tascar_hoadisplay tascar_oscctl test_render

#tascar_gpxvelocity \
tascar_gui tascar_pdf tascar_multipan tascar_jpos \
TEST_FILES = test_diffusereverb 
#test_async_file test_diffusereverb 

BINFILES += $(TEST_FILES)

ifeq "ok" "$(shell pkg-config gtkmm-3.0 && echo ok)"
GTKDEF = "-DGTKMM30"
GTKEXT = gtkmm-3.0
else
GTKDEF = "-DGTKMM24"
GTKEXT = gtkmm-2.4
endif

ifeq "ok" "$(shell test -e /usr/include/linuxtrack.h && echo  ok)"
LTRDEF = "-DLINUXTRACK"
LDLIBS += -llinuxtrack
endif

ifeq "$(DEBUG)" "yes"
CXXFLAGS += -g
endif

#BINFILES += `pkg-config gtkmm-3.0 && echo tascar_gui`

CXXFLAGS += $(GTKDEF) $(LTRDEF)

OBJECTS = jackclient.o coordinates.o delayline.o speakerlayout.o	\
multipan.o osc_helper.o async_file.o errorhandling.o scene.o		\
viewport.o audiochunks.o sinks.o acousticmodel.o multipan.o		\
multipan_amb3.o hoafilt.o xmlconfig.o osc_scene.o audioplayer.o		\
ringbuffer.o gammatone.o

INSTBIN = $(patsubst %,$(PREFIX)/bin/%,$(BINFILES))

#GTKMMBIN = tascar_gui

CXXFLAGS += -Wall -O3 -msse -msse2 -mfpmath=sse -ffast-math -fomit-frame-pointer -fno-finite-math-only -L./

EXTERNALS = jack libxml++-2.6 liblo sndfile
#EXTERNALS = jack liblo sndfile

#tascar_gui tascar_renderer tascar_audioplayer: OBJECTS += /usr/lib/libxml++-2.6.a
#tascar_gui tascar_renderer tascar_audioplayer: CXXFLAGS += -I/usr/include/libxml++-2.6/

tascar_hoadisplay: EXTERNALS += $(GTKEXT)

tascar_gui: EXTERNALS += $(GTKEXT)

tascar_pdf: EXTERNALS += cairomm-1.0

LDLIBS += -ltascar

LDLIBS += `pkg-config --libs $(EXTERNALS)`
CXXFLAGS += `pkg-config --cflags $(EXTERNALS)`

#CXXFLAGS += -ggdb

all:
	mkdir -p build
	$(MAKE) -C build -f ../Makefile $(BINFILES)

lib:
	mkdir -p build
	$(MAKE) -C build -f ../Makefile libtascar.a

libtascar.a: $(OBJECTS)
	ar rcs $@ $^

install:
	$(MAKE) -C build -f ../Makefile $(INSTBIN)

uninstall:
	rm -f $(INSTBIN)

clean:
	rm -Rf *~ src/*~ build doc/html

VPATH = ../src ../src/hoafilt

.PHONY : doc

doc:
	cd doc && sed -e 's/PROJECT.NUMBER.*=.*/&'`cat ../version`'/1' doxygen.cfg > .temp.cfg && doxygen .temp.cfg
	rm -Rf doc/.temp.cfg

include $(wildcard *.mk)

$(BINFILES): libtascar.a

$(PREFIX)/bin/%: %
	cp $< $@

%: %.o
	$(CXX) $(CXXFLAGS) $(LDLIBS) $^ -o $@

%.o: %.cc
	$(CPP) $(CPPFLAGS) -MM -MF $(@:.o=.mk) $<
	$(CXX) $(CXXFLAGS) -c $< -o $@

#dist: clean doc
dist: clean
	$(MAKE) -C doc
	$(MAKE) DISTNAME=tascar-`cat version` bz2

disttest:
	$(MAKE) DISTNAME=tascar-`cat version` disttest2

disttest2:
	rm -Rf $(DISTNAME) && tar xjf $(DISTNAME).tar.bz2
	cd $(DISTNAME) && $(MAKE)

bz2:
	rm -Rf $(DISTNAME) $(DISTNAME).files
	(cat files; find doc/html -print) | sed 's/.*/$(DISTNAME)\/&/g' > $(DISTNAME).files
	ln -s . $(DISTNAME)
	tar cjf $(DISTNAME).tar.bz2 --no-recursion -T $(DISTNAME).files
	rm -Rf $(DISTNAME) $(DISTNAME).files


tascar_ambdecoder: LDLIBS += `pkg-config --libs gsl`


# Local Variables:
# compile-command: "make"
# End: