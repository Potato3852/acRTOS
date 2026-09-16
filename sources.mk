ACRTOS_INCLUDES := -I$(ACRTOS_DIR)/include -I$(ACRTOS_DIR)/include/acrtos

ACRTOS_CXX_SOURCES := \
	$(ACRTOS_DIR)/src/scheduler.cpp \
	$(ACRTOS_DIR)/src/task.cpp \
	$(ACRTOS_DIR)/src/ipc.cpp \
	$(ACRTOS_DIR)/src/port_cm4.cpp \
	$(ACRTOS_DIR)/src/assert.cpp

ACRTOS_ASM_SOURCES := \
	$(ACRTOS_DIR)/src/port_asm.s
