BUILD?=build
DIST=$(BUILD)/dist

STATIC=$(addprefix $(BUILD)/,index.h app.h style.h)

SRC=main.cpp gst.cpp

DEPS=glib-2.0 gstreamer-1.0 gstreamer-rtp-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 json-glib-1.0 gstreamer-webrtc-nice-1.0
LIBSRC=build/httplib.cpp
LIBOBJ=$(LIBSRC:.cpp=.o)
LIBS=
INCL=
DEFS=GST_USE_UNSTABLE_API
CFLAGS=$(shell pkg-config --cflags $(DEPS)) $(addprefix -D,$(DEFS)) -pthread -gdwarf-4
LDFLAGS=$(shell pkg-config --libs $(DEPS)) $(addprefix -l,$(LIBS))

.PHONY: all clean

all: $(DIST)/wh

clean:
	rm -r $(BUILD)

$(DIST):
	mkdir -p $@

$(BUILD)/%.bin: static/%.html
	cp $< $@
	printf "\x00" >> $@

$(BUILD)/%.bin: static/js/%.js
	cp $< $@
	printf "\x00" >> $@

$(BUILD)/%.bin: static/css/%.css
	cp $< $@
	printf "\x00" >> $@

$(BUILD)/%.h: $(BUILD)/%.bin
	xxd -i $< $@

$(BUILD)/httplib.cpp:
	cd thirdparty/cpp-httplib && ./split.py -e cpp -o ../../$(BUILD)

$(BUILD)/%.o: $(BUILD)/%.cpp

$(DIST)/wh: $(DIST) $(STATIC) $(LIBOBJ) $(SRC)
	g++ $(CFLAGS) $(LIBOBJ) $(SRC) -o $@ $(LDFLAGS)
