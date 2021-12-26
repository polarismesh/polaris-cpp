BIT = $(shell getconf LONG_BIT)

POLARIS_CPP = ../..
POLARIS_INCL = $(POLARIS_CPP)/include
POLARIS_LIB_DIR = $(POLARIS_CPP)/build$(BIT)/lib
POLARIS_LIB = $(POLARIS_LIB_DIR)/libpolaris_api.a
PROTOBUF_LIB = $(POLARIS_CPP)/third_party/protobuf/build$(BIT)/libprotobuf.a

CXX = g++
CXXFLAGS += -g -Wall -Wno-write-strings -Werror -std=c++11

SRC = $(wildcard *.cpp)
OBJECTS = $(SRC:%.cpp=%)

all: $(OBJECTS)

%: %.cpp $(POLARIS_LIB)
	@echo -e Building $< ...
	$(CXX) $(CXXFLAGS) -I$(POLARIS_INCL) $< $(POLARIS_LIB) $(PROTOBUF_LIB) -pthread -lz -o $@

$(POLARIS_LIB):
	@echo build polaris-cpp lib
	make -C ${POLARIS_CPP}

clean:
	@echo -e Clean $(OBJECTS)
	@-rm -rf $(OBJECTS)
