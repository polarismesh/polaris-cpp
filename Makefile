include version.mk

CXX = g++
AR = ar
LONG_BIT = $(shell getconf LONG_BIT)

ifeq ($(M32), true)  # when build for i686 set M32=true
BIT = 32
M32_FLAGS = -m32 -march=i686 -D_FILE_OFFSET_BITS=$(LONG_BIT)
else
BIT = 64
endif

BUILD = ./build$(BIT)
OBJ_DIR = $(BUILD)/objs
LIB_DIR = $(BUILD)/lib
GEN_DIR = $(BUILD)/gens
TEST_DIR = $(BUILD)/test

CXXFLAGS += $(M32_FLAGS) -fPIC -pipe -fno-ident -D_REENTRANT -g -O2 -DREVISION=\"$(REVISION)\" \
						-W -Wall -Werror -std=c++11 -Woverloaded-virtual
LDFLAGS = -shared $(M32_FLAGS)
ARFLAGS = -rcs

ifeq ($(AddressSanitizer), true)
CXXFLAGS += -fsanitize=address
endif

ifeq ($(DisableTimeTicker), true)
CXXFLAGS += -DPOLARIS_DISABLE_TIME_TICKER # 禁用TimeTicker线程
endif

ifeq ($(COMPILE_FOR_PRE_CPP11), true)
LDFLAGS += -static-libstdc++ -Wl,--wrap=memcpy -lrt
CXXFLAGS += -DCOMPILE_FOR_PRE_CPP11
endif

ifeq ($(MemCheck), true)
VALGRIND = valgrind --leak-check=yes --error-exitcode=1  # 支持内存检查
endif

ifneq ($(TestCase),)
TESTCASE_PARA = --gtest_filter=$(TestCase)  # 支持指定测试用例
endif

ifneq ($(BmCase),)
BM_PARA = --benchmark_filter=$(BmCase)  # 支持指定测试用例
endif

ifeq ($(Coverage), true)	# 支持覆盖率
COVERAGE_FLAG = --coverage
endif

POLARIS_SLIB = $(LIB_DIR)/libpolaris_api.a
POLARIS_DLIB = $(LIB_DIR)/libpolaris_api.so
all: $(POLARIS_SLIB) $(POLARIS_DLIB)

###############################################################################
# yaml-cpp
YAML_DIR = third_party/yaml-cpp
YAML_INC_DIR = $(YAML_DIR)/include
YAML_LIB = $(YAML_DIR)/build$(BIT)/libyaml-cpp.a
YAML_SRC = $(wildcard $(YAML_DIR)/src/*.cpp)
YAML_OBJECTS = $(YAML_SRC:$(YAML_DIR)/src/%.cpp=$(YAML_DIR)/build$(BIT)/%.o)

$(YAML_LIB): $(YAML_OBJECTS)
	@echo "[YAML] Preparing yaml lib"
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $(YAML_OBJECTS)

YAML_CXXFLAGS = $(M32_FLAGS) -DNDEBUG -fPIC -pipe -fno-ident -D_REENTRANT -Wall
$(YAML_DIR)/build$(BIT)/%.o: $(YAML_DIR)/src/%.cpp
	@mkdir -p $(@D)
	@$(CXX) $(YAML_CXXFLAGS) -I$(YAML_INC_DIR) -o $@ -c $<

###############################################################################
# nghttp2
NGHTTP2_DIR = third_party/nghttp2/lib
NGHTTP2_INC_DIR = $(NGHTTP2_DIR)/includes
NGHTTP2_LIB = $(NGHTTP2_DIR)/build$(BIT)/libnghttp2.a
NGHTTP2_SRC = $(wildcard $(NGHTTP2_DIR)/*.c)
NGHTTP2_OBJECTS = $(NGHTTP2_SRC:$(NGHTTP2_DIR)/%.c=$(NGHTTP2_DIR)/build$(BIT)/%.o)

$(NGHTTP2_LIB): $(NGHTTP2_OBJECTS)
	@echo "[NGHTTP2] Preparing nghttp2 lib"
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $(NGHTTP2_OBJECTS)

NGHTTP2_CFLAGS = $(M32_FLAGS) -DNDEBUG -fPIC -pipe -fno-ident -D_REENTRANT
$(NGHTTP2_DIR)/build$(BIT)/%.o: $(NGHTTP2_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(NGHTTP2_CFLAGS) -I$(NGHTTP2_DIR)/includes -o $@ -c $<

###############################################################################
# murmurhash
MURMUR_DIR = third_party/murmurhash
MURMUR_INC_DIR = $(MURMUR_DIR)/src
MURMUR_LIB = $(MURMUR_DIR)/build$(BIT)/libmurmurhash.a
MURMUR_SRC = $(MURMUR_DIR)/src/MurmurHash3.cpp # 只编译这一个文件
MURMUR_OBJECTS = $(MURMUR_SRC:$(MURMUR_DIR)/src/%.cpp=$(MURMUR_DIR)/build$(BIT)/%.o)

$(MURMUR_LIB): $(MURMUR_OBJECTS)
	@echo "[MURMUR] Preparing murmurhash lib"
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $(MURMUR_OBJECTS)

$(MURMUR_DIR)/build$(BIT)/%.o:$(MURMUR_DIR)/src/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -DNDEBUG -Wno-implicit-fallthrough -I$(MURMUR_INC_DIR) -o $@ -c $<

###############################################################################
# re2
RE2_DIR = third_party/re2
RE2_INC_DIR = $(RE2_DIR)/
RE2_LIB = $(RE2_DIR)/build$(BIT)/libre2.a
RE2_OFILES=util/hash util/logging util/rune util/stringprintf util/strutil util/valgrind\
  re2/bitstate re2/compile re2/dfa re2/filtered_re2 re2/mimics_pcre re2/nfa re2/onepass\
	re2/parse re2/perl_groups re2/prefilter re2/prefilter_tree re2/prog re2/re2 re2/regexp\
	re2/set re2/simplify re2/stringpiece re2/tostring re2/unicode_casefold re2/unicode_groups
RE2_OBJECTS=$(patsubst %, $(RE2_DIR)/build$(BIT)/%.o, $(RE2_OFILES))

$(RE2_LIB): $(RE2_OBJECTS)
	@echo "[RE2] Preparing re2 lib"
	@echo $(RE2_OBJECTS)
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $(RE2_OBJECTS)

RE2_CXXFLAGS = $(M32_FLAGS) -fPIC -O3 -pthread -DNDEBUG -Wall -Wextra\
  -Wno-implicit-fallthrough -Wno-unused-parameter -Wno-missing-field-initializers
$(RE2_DIR)/build$(BIT)/%.o: $(RE2_DIR)/%.cc
	@mkdir -p $(@D)
	$(CXX) $(RE2_CXXFLAGS) -I$(RE2_INC_DIR) -o $@ -c $<

###############################################################################
# protobuf
PROTOBUF_DIR = third_party/protobuf
PROTOBUF_INC_DIR = $(PROTOBUF_DIR)/src
PROTOBUF_LIB = $(PROTOBUF_DIR)/build$(BIT)/libprotobuf.a
PROTOC = $(PROTOBUF_DIR)/build$(LONG_BIT)/protoc

$(PROTOBUF_DIR)/build$(LONG_BIT)/protoc: $(PROTOBUF_DIR)/build$(LONG_BIT)/libprotobuf.a

$(PROTOBUF_DIR)/build64/libprotobuf.a: $(PROTOBUF_DIR)/configure
	@echo "[PROTOBUF] Preparing protobuf 64bit lib and protoc"
	@cd $(PROTOBUF_DIR); ./configure --with-pic --disable-shared --enable-static
	@make -C $(PROTOBUF_DIR) clean
	@make -C $(PROTOBUF_DIR)
	mkdir -p $(@D); cp $(PROTOBUF_DIR)/src/.libs/libprotobuf.a $@
	cp $(PROTOBUF_DIR)/src/protoc $(PROTOBUF_DIR)/build64/protoc

$(PROTOBUF_DIR)/build32/libprotobuf.a: $(PROTOBUF_DIR)/configure
	@echo "[PROTOBUF] Preparing protobuf 32 lib and protoc"
	@cd $(PROTOBUF_DIR); ./configure --with-pic --disable-shared --enable-static \
		"CFLAGS=-m32" "CXXFLAGS=-m32" "LDFLAGS=-m32"
	@make -C $(PROTOBUF_DIR) clean
	@make -C $(PROTOBUF_DIR)
	mkdir -p $(@D); cp $(PROTOBUF_DIR)/src/.libs/libprotobuf.a $@
	cp $(PROTOBUF_DIR)/src/protoc $(PROTOBUF_DIR)/build32/protoc

$(PROTOBUF_DIR)/configure:
	echo "[AUTOGEN] Preparing protobuf"
	cd $(PROTOBUF_DIR); autoreconf -f -i -Wall,no-obsolete
###############################################################################
PROTO_FILE_DIR = polaris/proto

# gen v1 proto
PROTO_V1_DIR = $(PROTO_FILE_DIR)/v1
PROTO_V1_FILES = $(filter-out $(PROTO_V1_DIR)/%rpcapi.proto , $(wildcard $(PROTO_V1_DIR)/*.proto))
PROTO_V1_SRCS = $(patsubst $(PROTO_V1_DIR)/%.proto, $(GEN_DIR)/v1/%.pb.cc, $(PROTO_V1_FILES))
PROTO_V1_OBJECTS = $(patsubst $(PROTO_V1_DIR)/%.proto, $(OBJ_DIR)/v1/%.pb.o, $(PROTO_V1_FILES))

.SECONDARY: $(PROTO_V1_SRCS)
$(GEN_DIR)/v1/%.pb.cc: $(PROTO_V1_DIR)/%.proto $(PROTOC)
	@mkdir -p $(@D)
	$(PROTOC) --cpp_out=$(GEN_DIR) -I $(PROTO_FILE_DIR) -I $(PROTOBUF_INC_DIR) $<

$(OBJ_DIR)/v1/%.pb.o: $(GEN_DIR)/v1/%.pb.cc $(PROTO_V1_SRCS) $(PROTOBUF_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -isystem $(PROTOBUF_INC_DIR) -isystem $(GEN_DIR) -o $@ -c $<

# # gen v2 proto
PROTO_V2_DIR = $(PROTO_FILE_DIR)/v2
PROTO_V2_FILES = $(filter-out $(PROTO_V2_DIR)/%rpcapi.proto , $(wildcard $(PROTO_V2_DIR)/*.proto))
PROTO_V2_SRCS = $(patsubst $(PROTO_V2_DIR)/%.proto, $(GEN_DIR)/v2/%.pb.cc, $(PROTO_V2_FILES))
PROTO_V2_OBJECTS = $(patsubst $(PROTO_V2_DIR)/%.proto, $(OBJ_DIR)/v2/%.pb.o, $(PROTO_V2_FILES))

.SECONDARY: $(PROTO_V2_SRCS)
$(GEN_DIR)/v2/%.pb.cc: $(PROTO_V2_DIR)/%.proto $(PROTOC)
	@mkdir -p $(@D)
	$(PROTOC) --cpp_out=$(GEN_DIR) -I $(PROTO_FILE_DIR) -I $(PROTOBUF_INC_DIR) $<

$(OBJ_DIR)/v2/%.pb.o: $(GEN_DIR)/v2/%.pb.cc $(PROTO_V2_SRCS) $(PROTOBUF_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -isystem $(PROTOBUF_INC_DIR) -isystem $(GEN_DIR) -o $@ -c $<

###############################################################################
# polaris
POLARIS_HDR_DIR = include
POLARIS_INC = -I$(POLARIS_HDR_DIR) -Ipolaris -isystem $(GEN_DIR)
THIRD_PARTY_INC = -isystem $(YAML_INC_DIR) -isystem $(NGHTTP2_INC_DIR) \
									-isystem $(MURMUR_INC_DIR) -isystem $(PROTOBUF_INC_DIR) -isystem $(RE2_INC_DIR)
THIRD_PARTY_LIB = $(YAML_LIB) $(NGHTTP2_LIB) $(MURMUR_LIB) $(PROTOBUF_LIB) $(RE2_LIB)
THIRD_PARTY_OBJ = $(YAML_OBJECTS) $(NGHTTP2_OBJECTS) $(MURMUR_OBJECTS) $(RE2_OBJECTS)

SRC_SUBDIRS = $(shell find polaris -maxdepth 5 -type d -not -path "polaris/proto*")
SRC += $(foreach dir, $(SRC_SUBDIRS), $(wildcard $(dir)/*.cpp))
PROTO_OBJECTS = $(PROTO_V1_OBJECTS) $(PROTO_V2_OBJECTS)
OBJECTS := $(PROTO_OBJECTS) $(SRC:polaris/%.cpp=$(OBJ_DIR)/%.o)

ifeq ($(OnlyRateLimit), true)
OBJECTS := $(filter-out $(OBJ_DIR)/api/provider_api.o \
 					$(OBJ_DIR)/api/c_api.o,$(OBJECTS))
CXXFLAGS += -DONLY_RATE_LIMIT
endif

$(POLARIS_DLIB): $(OBJECTS) $(THIRD_PARTY_LIB)
	@mkdir -p $(@D)
	@echo "[Polaris] build polaris dynamic lib..."
	@$(CXX) $(LDFLAGS) $(OBJECTS) $(THIRD_PARTY_LIB) -o $@

$(POLARIS_SLIB): $(OBJECTS) $(THIRD_PARTY_LIB)
	@mkdir -p $(@D)
	@echo "[Polaris] build polaris static lib..."
	@$(AR) $(ARFLAGS) $@ $(OBJECTS) $(THIRD_PARTY_OBJ)

$(OBJ_DIR)/%.o: polaris/%.cpp $(PROTO_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(COVERAGE_FLAG) $(POLARIS_INC) $(THIRD_PARTY_INC) -o $@ -c $<

###############################################################################
# google test header files and libs
GTEST_DIR = third_party/googletest/googletest
GMOCK_DIR = third_party/googletest/googlemock
GTEST_INC = -isystem $(GTEST_DIR)/include -isystem $(GMOCK_DIR)/include
GTEST_BUILD = third_party/googletest/build$(BIT)
GTEST_LIB = $(GTEST_BUILD)/libgtest.a $(GTEST_BUILD)/libgmock.a

$(GTEST_BUILD)/libgtest.a: ${GTEST_DIR}/src/gtest-all.cc
	@mkdir -p $(@D)
	$(CXX) -isystem ${GTEST_DIR}/include -I${GTEST_DIR} -pthread -o $(GTEST_BUILD)/gtest-all.o -c $<
	$(AR) $(ARFLAGS) $@ $(GTEST_BUILD)/gtest-all.o

$(GTEST_BUILD)/libgmock.a: ${GMOCK_DIR}/src/gmock-all.cc
	@mkdir -p $(@D)
	$(CXX) -isystem ${GTEST_DIR}/include -I${GTEST_DIR} -isystem ${GMOCK_DIR}/include -I${GMOCK_DIR} \
		-pthread -o $(GTEST_BUILD)/gmock-all.o -c $< 
	$(AR) $(ARFLAGS) $@ $(GTEST_BUILD)/gmock-all.o

###############################################################################
# test
TEST_SUBDIRS = $(shell find test -type d ! -path "test/benchmark*" ! \
 								-path "test/chaos*" ! -path "test/integration*")
TEST_SRC = $(foreach dir, $(TEST_SUBDIRS), $(wildcard $(dir)/*.cpp))
TEST_TARGET = $(TEST_SRC:test/%.cpp=test/%)

TEST_OBJECTS = $(POLARIS_SLIB)

test: $(TEST_OBJECTS) $(GTEST_LIB) $(GTEST_MAIN) $(TEST_TARGET)

GTEST_MAIN = $(TEST_DIR)/gtest_mian.o
$(GTEST_MAIN): test/gtest_main.cc
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(POLARIS_INC) $(THIRD_PARTY_INC) $(GTEST_INC) -o $@ -c $<

test/%: test/%.cpp $(TEST_OBJECTS) $(GTEST_LIB) $(GTEST_MAIN)
	@echo "[TEST]    Testing $@"
	@mkdir -p $(TEST_DIR)/$(@D)
	$(CXX) $(CXXFLAGS) $(COVERAGE_FLAG) $(GTEST_INC) $(POLARIS_INC) $(THIRD_PARTY_INC) -Itest $< \
		$(GTEST_MAIN) $(TEST_OBJECTS) $(GTEST_LIB) $(PROTOBUF_LIB) -pthread -lz -lrt -o $(TEST_DIR)/$@
	@($(VALGRIND) $(TEST_DIR)/$@ $(TESTCASE_PARA)) || ( echo test $@ failed ; exit 1 )

###############################################################################
# integration test
INTEGRATION_DIR = test/integration
INTEGRATION_COMMON_SRC = $(wildcard $(INTEGRATION_DIR)/common/*.cpp)
INTEGRATION_COMMON = $(INTEGRATION_COMMON_SRC:test/integration/%.cpp=$(TEST_DIR)/integration/%.o)
INTEGRATION_SUBDIRS = $(shell find test/integration -type d ! -path "test/integration/common*")
INTEGRATION_SRC = $(foreach dir, $(INTEGRATION_SUBDIRS), $(wildcard $(dir)/*.cpp))
INTEGRATION_TARGET = $(INTEGRATION_SRC:test/integration/%.cpp=test/integration/%)

integration: $(TEST_OBJECTS) $(GTEST_LIB) $(INTEGRATION_COMMON) $(INTEGRATION_TARGET) 

$(TEST_DIR)/integration/common/%.o: test/integration/common/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(POLARIS_INC) $(THIRD_PARTY_INC) $(GTEST_INC) -Itest -o $@ -c $<

test/integration/%: $(INTEGRATION_DIR)/%.cpp $(TEST_OBJECTS) $(INTEGRATION_COMMON) $(GTEST_LIB)
	@echo "[INTEGRATION]    Testing $@"
	@mkdir -p $(TEST_DIR)/$(@D)
	$(CXX) $(CXXFLAGS) $(COVERAGE_FLAG) $(GTEST_INC) $(POLARIS_INC) $(THIRD_PARTY_INC) -Itest $< \
		$(INTEGRATION_COMMON) $(TEST_OBJECTS) $(GTEST_LIB) $(PROTOBUF_LIB) \
		-pthread -lz -lrt -o $(TEST_DIR)/$@
	@($(VALGRIND) $(TEST_DIR)/$@ $(TESTCASE_PARA)) || ( echo test $@ failed ; exit 1 )

###############################################################################
# coverage colloct
COVERAGE_DIR ?= $(TEST_DIR)/coverage
ifeq ($(shell expr $(shell g++ -dumpversion) '>=' 4.8.1), 1)  # check whether g++ support c++11
COVERAGE_BRANCH := --rc lcov_branch_coverage=1 # 低版本默认开启
GENHTML_BRANCH := --rc genhtml_branch_coverage=1
endif

coverage:
	@mkdir -p $(COVERAGE_DIR)/report
	@mv *.gcno *.gcda $(COVERAGE_DIR)
	@lcov $(COVERAGE_BRANCH) -c -d $(BUILD) -b ./ -o $(COVERAGE_DIR)/all.info
	@lcov $(COVERAGE_BRANCH) --remove $(COVERAGE_DIR)/all.info '*/protobuf/*' \
		-o $(COVERAGE_DIR)/filtered.info
	@lcov $(COVERAGE_BRANCH) -e $(COVERAGE_DIR)/filtered.info "*/polaris/*" \
	 	-o $(COVERAGE_DIR)/coverage.info
	@genhtml $(GENHTML_BRANCH) $(COVERAGE_DIR)/coverage.info -o $(COVERAGE_DIR)/report

###############################################################################
# benchmark
BENCHMARK_DIR = third_party/benchmark
BENCHMARK_INC := -isystem $(BENCHMARK_DIR)/include
BENCHMARK_LIB := $(BENCHMARK_DIR)/lib/libbenchmark_main.a
BENCHMARK_SRC := $(wildcard $(BENCHMARK_DIR)/src/*.cc)
BENCHMARK_OBJECTS = $(BENCHMARK_SRC:$(BENCHMARK_DIR)/src/%.cc=$(BENCHMARK_DIR)/build/%.o)

$(BENCHMARK_LIB): $(BENCHMARK_OBJECTS)
	@echo "[Benchmark] Preparing Benchmark lib"
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $(BENCHMARK_OBJECTS)

$(BENCHMARK_DIR)/build/%.o: $(BENCHMARK_DIR)/src/%.cc
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) --std=c++11 -Wno-error=deprecated-declarations -DNDEBUG $(BENCHMARK_INC) \
	  -I${BENCHMARK_DIR}/src -o $@ -c $<

BENCHMARK_TEST_SRC = $(wildcard test/benchmark/*.cpp)
BENCHMARK_TEST_TARGET = $(BENCHMARK_TEST_SRC:test/benchmark/%.cpp=benchmark/%)

benchmark: $(BENCHMARK_TEST_TARGET)

benchmark/%: test/benchmark/%.cpp $(BENCHMARK_LIB) $(POLARIS_SLIB)
	@echo "[BENCHMARK_TEST]    Benchmark testing $@"
	@mkdir -p $(TEST_DIR)/benchmark
	$(CXX) $(CXXFLAGS) $(BENCHMARK_INC) $(POLARIS_INC) $(THIRD_PARTY_INC) -Itest $< \
		$(BENCHMARK_LIB) $(POLARIS_SLIB) $(PROTOBUF_LIB) -pthread -lz -lrt -o $(TEST_DIR)/$@
	@$(TEST_DIR)/$@ $(BM_PARA) || ( echo benchmark test $@ failed ; exit 1 )

###############################################################################
# examples
EXAMPLES_SUBDIRS = $(shell find examples -maxdepth 5 -type d -not -path examples/c \
 										-not -path examples/spp)
EXAMPLES_SRC := $(foreach dir, $(EXAMPLES_SUBDIRS), $(wildcard $(dir)/*.cpp))
EXAMPLES_TARGET := $(EXAMPLES_SRC:examples/%.cpp=$(BUILD)/examples/%)

examples: $(EXAMPLES_TARGET) $(POLARIS_SLIB)

examples/%: $(BUILD)/examples/%
	@echo build $@.cpp success

# 静态库方式编译例子
$(BUILD)/examples/%: examples/%.cpp $(POLARIS_SLIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(POLARIS_HDR_DIR) $< $(POLARIS_SLIB) $(PROTOBUF_LIB) -pthread -lz -lrt -o $@

#动态库方式编译例子
# $(BUILD)/examples/%: examples/%.cpp $(POLARIS_DLIB)
# 	@mkdir -p $(@D)
# 	$(CXX) $(CXXFLAGS) -I$(POLARIS_HDR_DIR) $< -L$(LIB_DIR) -lpolaris_api -pthread -lz -lrt -o $@

###############################################################################
# chaos
chaos: $(POLARIS_SLIB)
	@make -C test/chaos clean
	@make -C test/chaos

###############################################################################
# package
package_name ?= polaris_cpp_sdk
package: all
	mkdir -p $(BUILD)/$(package_name)/include $(BUILD)/$(package_name)/dlib \
				$(BUILD)/$(package_name)/slib
	cp -r include/* $(BUILD)/$(package_name)/include
	cp $(POLARIS_DLIB) $(BUILD)/$(package_name)/dlib/
	cp $(POLARIS_SLIB) $(PROTOBUF_LIB) $(BUILD)/$(package_name)/slib/
	tar czf $(package_name).tar.gz -C $(BUILD) $(package_name)

###############################################################################

.PHONY: doc
doc:
	doxygen Doxyfile

clean:
	rm -rf build32
	rm -rf build64
	rm -rf doc/html
	rm -rf *.tar.gz
