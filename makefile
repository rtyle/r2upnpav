# ubuntu:
#	apt-get install libgupnp-av-1.0
#	apt-get install liblircclient-dev
#	apt-get install libboost-all-dev
# archlinux:
#	pacman -S gupnp-av
#	pacman -S lirc-utils
#	pacman -S boost-libs
#	pacman -S boost
#	pacman -S pkg-config
#	pacman -S gcc

CFLAGS = $(shell pkg-config --cflags gupnp-av-1.0 libcec) -I /usr/include/lirc -g -std=c++0x
LDLIBS = $(shell pkg-config --libs   gupnp-av-1.0 libcec) -lboost_program_options -lboost_regex -lboost_system -llirc_client

r2upnpav: r2upnpav.cc
	$(CXX) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm r2upnpav
