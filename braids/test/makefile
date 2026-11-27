PACKAGES       = braids/test stmlib/utils braids

VPATH          = $(PACKAGES)

TARGET         = oscillator_test
BUILD_ROOT     = build/
BUILD_DIR      = $(BUILD_ROOT)$(TARGET)/
CC_FILES       = analog_oscillator.cc \
		digital_oscillator.cc \
		resources.cc \
		macro_oscillator.cc \
		oscillator_test.cc \
		random.cc
OBJ_FILES      = $(CC_FILES:.cc=.o)
OBJS           = $(patsubst %,$(BUILD_DIR)%,$(OBJ_FILES)) $(STARTUP_OBJ)
DEPS           = $(OBJS:.o=.d)
DEP_FILE       = $(BUILD_DIR)depends.mk

all:  oscillator_test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)%.o: %.cc
	g++ -c -DTEST -g -Wall -Werror -Wno-unused-variable -I. $< -o $@

$(BUILD_DIR)%.d: %.cc
	g++ -MM -DTEST -I. $< -MF $@ -MT $(@:.d=.o)

oscillator_test:  $(OBJS)
	g++ -o $(TARGET) $(OBJS)

# MutatedHypnoise: engine tests, writes a .wav file.
HYPNOISE_SRCS  = braids/test/hypnoise_test.cc 		braids/analog_oscillator.cc 		braids/digital_oscillator.cc 		braids/macro_oscillator.cc 		braids/resources.cc 		braids/engine_params.cc 		stmlib/utils/random.cc

hypnoise_test:  $(HYPNOISE_SRCS)
	g++ -DTEST -O2 -g -Wall -Werror -Wno-unused-variable -Wno-unused-but-set-variable -I. $(HYPNOISE_SRCS) -o hypnoise_test

depends:  $(DEPS)
	cat $(DEPS) > $(DEP_FILE)

$(DEP_FILE):  $(BUILD_DIR) $(DEPS)
	cat $(DEPS) > $(DEP_FILE)

include $(DEP_FILE)
