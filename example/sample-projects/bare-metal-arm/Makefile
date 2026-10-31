# Builds the same application as CMakeLists.txt, without CMake: the way a
# Makefile, Keil or IAR project would use DelegateMQ.
#
#   - DelegateMQ is one source file, src/DelegateMQ.cpp.
#   - Options come from config/DelegateMQConfig.h (picked up automatically
#     because config/ is on the include path), not from -D flags.
#
# Usage (needs arm-none-eabi-gcc, and a POSIX shell for mkdir/rm):
#   make            build build-make/delegate_app
#   make run        build and run on QEMU
#   make clean

DMQ_SRC  := ../../../src
BUILD    := build-make
TARGET   := $(BUILD)/delegate_app

CC       := arm-none-eabi-gcc
CXX      := arm-none-eabi-g++

ARCH     := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 --specs=nano.specs
INCLUDES := -I$(DMQ_SRC)/delegate-mq -Iconfig
CFLAGS   := $(ARCH) -Wall
CXXFLAGS := $(ARCH) -Wall -std=gnu++20 $(INCLUDES)
LDFLAGS  := $(ARCH) -Tflash.ld -Wl,-Map=$(BUILD)/output.map
LDLIBS   := -lc -lgcc -lnosys

OBJS := $(BUILD)/main.o $(BUILD)/retarget.o $(BUILD)/startup.o $(BUILD)/DelegateMQ.o

all: $(TARGET)

$(TARGET): $(OBJS) flash.ld
	$(CXX) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(BUILD)/main.o: main.cpp config/DelegateMQConfig.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/DelegateMQ.o: $(DMQ_SRC)/DelegateMQ.cpp config/DelegateMQConfig.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

run: $(TARGET)
	qemu-system-arm -M mps2-an386 -cpu cortex-m4 -kernel $(TARGET) -nographic -semihosting-config enable=on,target=native

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
