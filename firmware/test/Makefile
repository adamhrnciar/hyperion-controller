# SPDX-License-Identifier: GPL-3.0-or-later
# Host unit tests for firmware/src/core. Needs only a host C compiler.
#   make -C firmware/test

CC      ?= cc
CFLAGS  ?= -std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion
CORE    := ../src/core
SRCS    := test_core.c $(CORE)/rc.c $(CORE)/sbus.c $(CORE)/crsf.c $(CORE)/arming.c $(CORE)/mixer.c
BUILD   := build

.PHONY: test clean
test: $(BUILD)/test_core
	./$(BUILD)/test_core

$(BUILD)/test_core: $(SRCS) $(wildcard $(CORE)/*.h)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -I$(CORE) -o $@ $(SRCS)

clean:
	rm -rf $(BUILD)
