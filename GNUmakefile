#
# (C) Copyright T.Schwinger - All rights reserved.
#
# Work sample - for evaluation purposes only.
#

.PHONY: all clean

executable = synth
sources = synth.cpp audio_output.cpp wav_export.cpp

uname=$(shell uname)

CXXFLAGS=-std=c++23 -Wall -pedantic -g

ifeq ($(uname), Darwin)
LDFLAGS+=-framework OpenAL -lpthread -lm
else
LDFLAGS+=-L/usr/local/lib -lopenal -lpthread -lm
CXXFLAGS+=-I/usr/local/include
endif

all: $(executable)

$(executable): $(sources:.cpp=.o)
	$(CXX) $^ -o $@ $(LDFLAGS)

clean:
	$(RM) $(executable)
	$(RM) $(sources:.cpp=.dep)
	$(RM) $(sources:.cpp=.o)

%.dep: %.cpp
	$(CXX) -MM $(CXXFLAGS) $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(sources:.cpp=.dep)

