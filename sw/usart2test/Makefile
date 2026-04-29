RISCV_PREFIX ?= riscv32-unknown-elf-
CC  = $(RISCV_PREFIX)gcc
AS  = $(RISCV_PREFIX)gcc
LD  = $(RISCV_PREFIX)gcc

# rv32imac_zicsr: base + multiply + atomics + compressed + CSR instructions
ARCH    = rv32imac_zicsr
ABI     = ilp32
CFLAGS  = -march=$(ARCH) -mabi=$(ABI) -O1 -Wall
ASFLAGS = -march=$(ARCH) -mabi=$(ABI)
LDFLAGS = -march=$(ARCH) -mabi=$(ABI) -nostartfiles -static -no-pie \
          -Wl,--no-relax -T link.ld

TARGET  = main.elf
OBJS    = bootstrap.o main.o

VP      ?= usart2test-vp
VP_BIN  ?= ../../vp/build/bin/$(VP)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^
	$(RISCV_PREFIX)size $@

bootstrap.o: bootstrap.S
	$(AS) $(ASFLAGS) -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	PATH="$(HOME)/.local/bin:$(PATH)" $(VP_BIN) $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run clean
