# Standalone visible PPU pixel regression target for reusable authored RSX owners.
# The checkout and fixture are mounted read-only at /src; all products go under /out.

.SUFFIXES:

ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT must be set by the locked helengine-ps3-sdk image)
endif
ifneq ($(SRC_ROOT),/src)
$(error SRC_ROOT must be /src in the read-only source mount)
endif
ifneq ($(BUILD_ROOT),/out)
$(error BUILD_ROOT must be /out in the writable output mount)
endif

include $(PSL1GHT)/ppu_rules

TARGET := ps3_authored_gpu_tests
ELF := $(BUILD_ROOT)/$(TARGET).elf
SELF_OUT := $(BUILD_ROOT)/$(TARGET).self
FAKE_SELF_OUT := $(BUILD_ROOT)/$(TARGET).fake.self
OBJECT_ROOT := $(BUILD_ROOT)/obj
SELF_STAGE := $(BUILD_ROOT)/self-stage
SOURCES := \
	$(SRC_ROOT)/src/platform/ps3/Ps3AuthoredProgram.cpp \
	$(SRC_ROOT)/src/platform/ps3/Ps3AuthoredTexture.cpp \
	$(SRC_ROOT)/src/platform/ps3/Ps3ShaderArtifactReader.cpp \
	$(SRC_ROOT)/src/platform/ps3/Ps3RsxProgramValidator.cpp \
	$(SRC_ROOT)/src/platform/ps3/Ps3Video.cpp \
	$(SRC_ROOT)/tests/native/Ps3AuthoredGpuTests.cpp
OBJECTS := $(patsubst $(SRC_ROOT)/%.cpp,$(OBJECT_ROOT)/%.o,$(SOURCES))
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -O2 -g -mcpu=cell $(MACHDEP) $(LIBPSL1GHT_INC) -I$(SRC_ROOT)/src
LIBS := -lrsx -lgcm_sys -lsysutil -lrt -llv2 -lm

.PHONY: all
all: $(SELF_OUT)

$(OBJECT_ROOT)/%.o: $(SRC_ROOT)/%.cpp
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(ELF): $(OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CXX) $(OBJECTS) $(MACHDEP) $(LIBPSL1GHT_LIB) $(LIBS) -o "$@"

$(SELF_OUT): $(ELF)
	@mkdir -p "$(SELF_STAGE)"
	$(STRIP) "$<" -o "$(SELF_STAGE)/$(TARGET).elf"
	$(SPRX) "$(SELF_STAGE)/$(TARGET).elf"
	$(SELF) "$(SELF_STAGE)/$(TARGET).elf" "$@"
	$(FSELF) "$(SELF_STAGE)/$(TARGET).elf" "$(FAKE_SELF_OUT)"

-include $(OBJECTS:.o=.d)
