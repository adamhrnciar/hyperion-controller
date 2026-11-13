# SPDX-License-Identifier: GPL-3.0-or-later
# Runs the unmodified firmware on a model of the board. Needs a host C compiler
# and the assembled PIO program (pioasm, built by the normal firmware build).
#   make -C firmware/sim
#   make -C firmware/sim PIOASM=/path/to/pioasm
#   build/failsafe_sbus -v dropout      # one scenario, with the firmware console

CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter
SRC     := ../src
BUILD   := build
PIOASM  ?= $(firstword $(wildcard ../build/pioasm/pioasm) $(shell command -v pioasm 2>/dev/null))

FW_SRCS := main.c bringup.c drive.c motor.c servo_out.c sense.c imu.c rc_input.c sbus_pio.c \
           crsf_uart.c rc_pwm.c core/rc.c core/sbus.c core/crsf.c core/arming.c core/mixer.c
SIM_SRCS := simhw.c scenarios.c

# name:MODE:PROTOCOL[:extra define]
VARIANTS := failsafe_sbus:FAILSAFE:SBUS failsafe_crsf:FAILSAFE:CRSF failsafe_pwm:FAILSAFE:PWM \
            drive_sbus:DRIVE:SBUS armswitch_sbus:FAILSAFE:SBUS:RC_CH_ARM=4 \
            rc_sbus:RC:SBUS rc_crsf:RC:CRSF rc_pwm:RC:PWM \
            blink:BLINK:SBUS adc:ADC:SBUS imu:IMU:SBUS motor:MOTOR:SBUS thermal:THERMAL:SBUS
NAMES   := $(foreach v,$(VARIANTS),$(word 1,$(subst :, ,$(v))))

.PHONY: test clean
test: $(addprefix $(BUILD)/,$(NAMES))
	@fail=0; for t in $(NAMES); do echo "== $$t"; $(BUILD)/$$t || fail=1; done; \
	if [ $$fail = 0 ]; then echo "ALL PASSED"; else echo "FAILURES"; exit 1; fi

$(BUILD)/sbus_rx.pio.h: $(SRC)/sbus_rx.pio
	@mkdir -p $(BUILD)
	@test -n "$(PIOASM)" || { echo "pioasm not found: build the firmware first or pass PIOASM=..."; exit 1; }
	$(PIOASM) -o c-sdk $< $@

define VARIANT_RULE
$(BUILD)/$(1): $(addprefix $(SRC)/,$(FW_SRCS)) $(SIM_SRCS) $(BUILD)/sbus_rx.pio.h \
               $(wildcard $(SRC)/*.h $(SRC)/core/*.h *.h include/*.h include/hardware/*.h include/pico/*.h)
	$(CC) $(CFLAGS) -Iinclude -I. -I$(SRC) -I$(BUILD) -include include/simhw.h -Dmain=firmware_main \
	    -DHYPERION_MODE=HYPERION_MODE_$(2) -DRC_PROTOCOL=RC_PROTOCOL_$(3) $(if $(4),-D$(4)) \
	    -o $$@ $(addprefix $(SRC)/,$(FW_SRCS)) $(SIM_SRCS) -lm
endef
$(foreach v,$(VARIANTS),$(eval $(call VARIANT_RULE,$(word 1,$(subst :, ,$(v))),$(word 2,$(subst :, ,$(v))),$(word 3,$(subst :, ,$(v))),$(word 4,$(subst :, ,$(v))))))

clean:
	rm -rf $(BUILD)
