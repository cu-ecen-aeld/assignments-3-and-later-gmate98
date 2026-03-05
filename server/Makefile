ifdef CROSS_COMPILE
CC ?= $(CROSS_COMPILE)gcc
SYSROOT := $(shell $(CC) -print-sysroot)
CFLAGS += --sysroot=$(SYSROOT)
LDFLAGS += --sysroot=$(SYSROOT)
else
CC ?= gcc
CFLAGS ?=
LDFLAGS ?=
endif

# Add all and extra warinigs
CFLAGS += -Wall -Wextra -O0 -g

SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)
TARGET ?= aesdsocket

.PHONY: all default clean

all: $(TARGET)
default: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking..."
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@echo "Cleaning..."
	@rm -f $(OBJS) $(TARGET)
