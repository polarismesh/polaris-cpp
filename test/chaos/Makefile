CXX = g++
CXXFLAGS += -fPIC -pipe -fno-ident -D_REENTRANT -g -O2 -W -Wall -Werror -std=c++11

POLARIS_DIR = ../../

POLARIS_HDR_DIR = $(POLARIS_DIR)/include
POLARIS_SLIB = $(POLARIS_DIR)/build64/lib/libpolaris_api.a
PROTOBUF_LIB = $(POLARIS_DIR)/third_party/protobuf/build64/libprotobuf.a


CHAOS_SRC := $(wildcard *.cpp)
CHAOS_TARGET := $(CHAOS_SRC:%.cpp=./build/%)

chaos: $(CHAOS_TARGET) $(POLARIS_SLIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(CHAOS_TARGET) $(POLARIS_SLIB) $(PROTOBUF_LIB) -pthread -lz -lrt -o $@

./build/%: %.cpp $(POLARIS_SLIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(POLARIS_HDR_DIR) -I. -c $< -o $@

clean:
	rm -rf chaos
	rm -rf ./build
