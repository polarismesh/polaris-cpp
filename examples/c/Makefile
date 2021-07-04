BIT = $(shell getconf LONG_BIT)
POLARIS_INCL = ../../include
POLARIS_LIB_DIR = ../../build$(BIT)/lib
POLARIS_LIB = $(POLARIS_LIB_DIR)/libpolaris_api.so

CC = gcc
CFLAGS += -g -Wall -Wno-write-strings -Werror

SRC = $(wildcard *.c)
OBJECTS = $(SRC:%.c=%)

all: $(OBJECTS)

%: %.c $(POLARIS_LIB)
	@echo -e Building $< ...
	$(CC) $(CFLAGS) -I$(POLARIS_INCL) $< -pthread -lz -lrt \
	-L$(POLARIS_LIB_DIR) -lpolaris_api  -Wl,-rpath $(POLARIS_LIB_DIR) -o $@

clean:
	@echo -e Clean $(OBJECTS)
	@-rm -rf $(OBJECTS)
