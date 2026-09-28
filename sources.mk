ACRTOS_ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

ACRTOS_INCLUDES := -I$(ACRTOS_ROOT)/include -I$(ACRTOS_ROOT)/include/acrtos

ACRTOS_SOURCES := \
    $(ACRTOS_ROOT)/src/assert.cpp \
    $(ACRTOS_ROOT)/src/ipc.cpp \
    $(ACRTOS_ROOT)/src/port_cm4.cpp \
    $(ACRTOS_ROOT)/src/queue.cpp \
    $(ACRTOS_ROOT)/src/scheduler.cpp \
    $(ACRTOS_ROOT)/src/task.cpp

ACRTOS_ASM_SOURCES := \
    $(ACRTOS_ROOT)/src/port_asm.s