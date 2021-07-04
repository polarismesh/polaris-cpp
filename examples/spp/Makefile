MT_INCL = /data/spp/include/sync_incl
MT_LIB = /data/spp/bin/lib
POLARIS_INCL = ../../include
POLARIS_LIB = ../../build$(BIT)/lib

CXX = g++
CFLAGS += -g -Wall -Wno-write-strings -Werror
INC += -I$(MT_INCL) -I$(POLARIS_INCL)
LIB += -L$(MT_LIB) -lmt -ldl -Wl,-rpath $(MT_LIB) \
 	-L$(POLARIS_LIB) -lpolaris_api -pthread -lz -Wl,-rpath $(POLARIS_LIB)

TARGET = mt_echo_consumer
OBJ = mt_echo_consumer.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CFLAGS) $(LIB) -o $@ $^

clean:
	@echo -e Clean ...
	@-rm *.o
	@-rm $(TARGET)

%.o: %.cpp
	@echo -e Compiling $< ...
	$(CXX) $(CFLAGS) -c -o $@ $< $(INC)
