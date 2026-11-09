CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror
TARGET := pcie_test

.PHONY: all clean

all: $(TARGET)

$(TARGET): pcie_test.c ../include/rk_pcie_uapi.h
	$(CC) $(CFLAGS) -o $@ pcie_test.c

clean:
	rm -f $(TARGET)
