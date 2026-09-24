##
## Makefile for STM32F429I-Discovery + FreeRTOS + LCD demo
## Toolchain: GNU Arm Embedded (arm-none-eabi-gcc) + GNU Make - both free/open-source.
##
TARGET = stm32F429freertos-lcd-demo

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc -x assembler-with-cpp
OBJCOPY = $(PREFIX)objcopy
OBJDUMP = $(PREFIX)objdump
SIZE    = $(PREFIX)size

BUILD_DIR = build

CORE_DIR      = Core
DRIVERS_DIR   = Drivers
MIDDLEWARE_DIR = Middlewares/Third_Party/FreeRTOS
BSP_DIR       = $(DRIVERS_DIR)/BSP/STM32F429I-Discovery
COMP_DIR      = $(DRIVERS_DIR)/BSP/Components
HAL_DIR       = $(DRIVERS_DIR)/STM32F4xx_HAL_Driver
CMSIS_DIR     = $(DRIVERS_DIR)/CMSIS

C_SOURCES = \
  $(CORE_DIR)/Src/main.c \
  $(CORE_DIR)/Src/stm32f4xx_it.c \
  $(CORE_DIR)/Src/system_stm32f4xx.c \
  $(filter-out %_template.c,$(wildcard $(HAL_DIR)/Src/*.c)) \
  $(BSP_DIR)/stm32f429i_discovery.c \
  $(BSP_DIR)/stm32f429i_discovery_lcd.c \
  $(BSP_DIR)/stm32f429i_discovery_sdram.c \
  $(BSP_DIR)/stm32f429i_discovery_io.c \
  $(BSP_DIR)/stm32f429i_discovery_ts.c \
  $(BSP_DIR)/stm32f429i_discovery_eeprom.c \
  $(BSP_DIR)/stm32f429i_discovery_gyroscope.c \
  $(COMP_DIR)/ili9341/ili9341.c \
  $(COMP_DIR)/stmpe811/stmpe811.c \
  $(COMP_DIR)/l3gd20/l3gd20.c \
  $(MIDDLEWARE_DIR)/Source/tasks.c \
  $(MIDDLEWARE_DIR)/Source/list.c \
  $(MIDDLEWARE_DIR)/Source/queue.c \
  $(MIDDLEWARE_DIR)/Source/timers.c \
  $(MIDDLEWARE_DIR)/Source/event_groups.c \
  $(MIDDLEWARE_DIR)/Source/stream_buffer.c \
  $(MIDDLEWARE_DIR)/Source/croutine.c \
  $(MIDDLEWARE_DIR)/Source/portable/GCC/ARM_CM4F/port.c \
  $(MIDDLEWARE_DIR)/Source/portable/MemMang/heap_4.c

ASM_SOURCES = $(CMSIS_DIR)/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f429xx.s

C_INCLUDES = \
  -I$(CORE_DIR)/Inc \
  -I$(HAL_DIR)/Inc \
  -I$(CMSIS_DIR)/Device/ST/STM32F4xx/Include \
  -I$(CMSIS_DIR)/Include \
  -I$(BSP_DIR) \
  -I$(COMP_DIR)/Common \
  -I$(COMP_DIR)/ili9341 \
  -I$(COMP_DIR)/stmpe811 \
  -I$(COMP_DIR)/l3gd20 \
  -I$(MIDDLEWARE_DIR)/Source/include \
  -I$(MIDDLEWARE_DIR)/Source/portable/GCC/ARM_CM4F

C_DEFS = -DSTM32F429xx -DUSE_HAL_DRIVER -DUSE_STM32F429I_DISCO

CPU     = -mcpu=cortex-m4
FPU     = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU     = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

WARNINGS = -Wall -Wextra -Wno-unused-parameter
OPT      = -Og -g3

CFLAGS  = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) $(WARNINGS) -ffunction-sections -fdata-sections -fno-common -std=gnu11 -MMD -MP
ASFLAGS = $(MCU) $(OPT)

LDSCRIPT = STM32F429ZITx_FLASH.ld
LDFLAGS  = $(MCU) -specs=nano.specs -specs=nosys.specs -T$(LDSCRIPT) \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections -lm

OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) $(LDSCRIPT)
	$(CC) $(MCU) $(OBJECTS) $(LDFLAGS) -o $@
	$(SIZE) $@

-include $(OBJECTS:.o=.d)

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary -S $< $@

$(BUILD_DIR):
	-mkdir $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

# Minimal firmware that only blanks the LCD, then halts. Used after erasing the
# board so the display doesn't keep showing the last image from before the erase.
clear:
	"$(MAKE)" BUILD_DIR=build-clear TARGET=clear-display C_DEFS="$(C_DEFS) -DCLEAR_DISPLAY_ONLY" all

# Flash with the free, open-source st-flash tool (stlink-org/stlink), if installed.
flash: $(BUILD_DIR)/$(TARGET).bin
	st-flash write $(BUILD_DIR)/$(TARGET).bin 0x08000000

.PHONY: all clean clear flash
