ifdef CROSS_COMPILE
CC := $(CROSS_COMPILE)gcc
SYSROOT := $(shell $(CC) -print-sysroot)
CFLAGS := --sysroot=$(SYSROOT)
LDFLAGS := --sysroot=$(SYSROOT)
else
CC := gcc
CFLAGS :=
LDFLAGS :=
endif

.PHONY: all clean

all: writer

writer: writer.o
	@echo "Linking..."
	$(CC) $(LDFLAGS) -o writer writer.o

writer.o: writer.c
	@echo "Compiling..."
	$(CC) $(CFLAGS) -c -o writer.o writer.c

clean:
	@echo "Cleaning..."
	@rm -f writer.o writer