
# MCU name
MCU = atmega324pa


# Processor frequency.
#     This will define a symbol, F_CPU, in all source code files equal to the
#     processor frequency in Hz. You can then use this symbol in your source
#     code to calculate timings. Do NOT tack on a 'UL' at the end, this will be
#     done automatically to create a 32-bit value in your source code.
#
#     This will be an integer division of F_CLOCK below, as it is sourced by
#     F_CLOCK after it has run through any CPU prescalers. Note that this value
#     does not *change* the processor frequency - it should merely be updated to
#     reflect the processor speed set externally so that the code can use
#     accurate software delays.
F_CPU = 8000000


# Input clock frequency.
#     This will define a symbol, F_CLOCK, in all source code files equal to the
#     input clock frequency (before any prescaling is performed) in Hz. This
#     value may differ from F_CPU if prescaling is used on the latter, and is
#     required as the raw input clock is fed directly to the PLL sections of the
#     AVR for high speed clock generation for the USB and other AVR subsections.
#     Do NOT tack on a 'UL' at the end, this will be done automatically to
#     create a 32-bit value in your source code.
#
#     If no clock division is performed on the input clock inside the AVR (via
#     the CPU clock adjust registers or the clock division fuses), this will
#     be equal to F_CPU.
F_CLOCK = 8000000

LDFLAGS += -T$(BOARD_DIR)/link.x

$(curdir)-y += board.c
$(curdir)-$(CONFIG_LIB_CONTIKI) += clock.c
$(curdir)-$(CONFIG_LIB_CONTIKI) += main.c

$(curdir)-y += $(IMAGE)/
$(eval $(call subdir,$(curdir)))

