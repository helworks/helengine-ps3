# Native PS3 build for the Helengine PS3 handler (v0.1, no Core).
#
# This Makefile runs only inside the locked helengine-ps3-sdk image, invoked by
# scripts/build.ps1 with the source tree mounted read-only at /src and an empty
# external output directory mounted read-write at /out. Every object, shader
# intermediate, ELF, SELF, map and dependency file is written under BUILD_ROOT.
#
# Rules follow the pinned PSL1GHT sample (samples/graphics/rsxtest/Makefile and
# ppu_rules), rewritten with absolute paths because the sample's recursive
# same-directory layout cannot write next to a read-only source tree.

.SUFFIXES:

ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT must be set; this Makefile runs inside the helengine-ps3-sdk image)
endif
ifndef BUILD_ROOT
$(error BUILD_ROOT is required; pass the external output directory, e.g. BUILD_ROOT=/out)
endif

SRC_ROOT ?= $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
TARGET := helengine_ps3

# Native builds the v0.1 host alone. Core additionally compiles a generated Core C++ tree
# supplied by the platform build and links it into the same executable.
MODE ?= Native
ifeq ($(filter Native Clear Cube Core,$(MODE)),)
$(error MODE must be Native, Clear, Cube or Core)
endif

include $(PSL1GHT)/ppu_rules

ELF := $(BUILD_ROOT)/$(TARGET).elf
SELF_OUT := $(BUILD_ROOT)/$(TARGET).self
FAKE_SELF_OUT := $(BUILD_ROOT)/$(TARGET).fake.self
MAP := $(BUILD_ROOT)/$(TARGET).map
OBJ_ROOT := $(BUILD_ROOT)/obj
SHADER_ROOT := $(BUILD_ROOT)/shaders
SELF_STAGE := $(BUILD_ROOT)/self-stage

# HELENGINE_DEFINES carries the explicit compile definitions selected by
# scripts/build.ps1 (mode, fixed pose, run duration, failure point).
HELENGINE_DEFINES ?=

# The native host is one entry point plus the focused types under src/platform/ps3.
SOURCES := $(sort $(wildcard $(SRC_ROOT)/src/*.cpp) $(wildcard $(SRC_ROOT)/src/platform/ps3/*.cpp))
OBJECTS := $(patsubst $(SRC_ROOT)/src/%.cpp,$(OBJ_ROOT)/%.o,$(SOURCES))

# --- Generated Core ---------------------------------------------------------------------
# The generated tree is compiled as the finalized unity translation unit plus every
# generated .cpp the unity file does not include. This split, and the disjointness check
# below, follow the Dreamcast Makefile, which is proven against a real generated tree.
ifeq ($(MODE),Core)
ifndef GENERATED_CORE_ROOT
$(error Core mode requires GENERATED_CORE_ROOT)
endif

GENERATED_FINALIZED_UNITY := $(GENERATED_CORE_ROOT)/helengine_core_unity.cpp
ifneq ($(wildcard $(GENERATED_FINALIZED_UNITY)),)
GENERATED_UNITY_SOURCE := $(GENERATED_FINALIZED_UNITY)
else
GENERATED_UNITY_SOURCE := $(GENERATED_CORE_ROOT)/generated_unity.cpp
endif
ifeq ($(wildcard $(GENERATED_UNITY_SOURCE)),)
$(error GENERATED_CORE_ROOT contains no unity translation unit: $(GENERATED_CORE_ROOT))
endif

GENERATED_CPP_SOURCES := $(shell find "$(GENERATED_CORE_ROOT)" -type f -name '*.cpp' ! -name 'helengine_core_unity.cpp' ! -name 'generated_unity.cpp' | sort)
GENERATED_UNITY_INCLUDES := $(shell tr -d '\r' < "$(GENERATED_UNITY_SOURCE)" | sed -n 's/^#include "\(.*\.cpp\)".*/\1/p')
GENERATED_EXTRA_SOURCES := $(filter-out $(GENERATED_UNITY_SOURCE) $(addprefix $(GENERATED_CORE_ROOT)/,$(GENERATED_UNITY_INCLUDES)),$(GENERATED_CPP_SOURCES))

GENERATED_UNITY_OBJECT := $(OBJ_ROOT)/generated/helengine_core_unity.o
GENERATED_EXTRA_OBJECTS := $(patsubst $(GENERATED_CORE_ROOT)/%.cpp,$(OBJ_ROOT)/generated-extra/%.o,$(GENERATED_EXTRA_SOURCES))
OBJECTS += $(GENERATED_UNITY_OBJECT) $(GENERATED_EXTRA_OBJECTS)
DEPS += $(GENERATED_UNITY_OBJECT:.o=.d) $(GENERATED_EXTRA_OBJECTS:.o=.d)

GENERATED_INCLUDES := -I$(GENERATED_CORE_ROOT) -I$(GENERATED_CORE_ROOT)/runtime
# Project code includes generated headers as system headers, so warnings inside machine
# output never fail this project's -Werror build. Generated code itself is compiled with
# ordinary -I, because its own warnings are worth seeing.
GENERATED_SYSTEM_INCLUDES := -isystem $(GENERATED_CORE_ROOT) -isystem $(GENERATED_CORE_ROOT)/runtime

# Two toolchain accommodations, both required by this cross toolchain rather than by the
# generated code, and both explained in Ps3NewlibMathCompat.hpp:
#   - libstdc++ hides its C99 math overloads behind a macro that is off in this build.
#   - five long-double entry points it then expects are absent from newlib.
NEWLIB_MATH_COMPAT := -D_GLIBCXX_USE_C99_MATH_TR1=1 -include $(SRC_ROOT)/src/platform/ps3/Ps3NewlibMathCompat.hpp

# The generated runtime uses C++20 requires-expressions, so the whole Core build is C++20.
# The GNU dialect rather than strict ISO: strict mode defines __STRICT_ANSI__, which makes
# newlib hide its POSIX declarations, and the generated file-stream code calls fileno,
# fstat and ftruncate whenever hosted filesystem support is on. The language features are
# identical between the two; only the visibility of platform extensions differs.
GENERATED_STANDARD := -std=gnu++20

# Generated code is machine output, not project code: it is compiled with warnings visible
# but never with -Werror, because a generator warning is fixed in the generator.
GENERATED_CXXFLAGS := $(GENERATED_STANDARD) -Wall -Wextra -O2 -g -mcpu=cell $(MACHDEP) -fexceptions -frtti \
    $(NEWLIB_MATH_COMPAT) $(GENERATED_INCLUDES) -isystem $(PSL1GHT)/ppu/include -I$(SRC_ROOT)/src -I$(SHADER_ROOT) $(HELENGINE_DEFINES)
endif
DEPENDS := $(OBJECTS:.o=.d)

# Shader sources compiled through the pinned SDK conversion rules. Each .vcg/.fcg
# becomes a .vpo/.fpo binary, which bin2s turns into an object plus a generated
# header (<name>_vpo.h / <name>_fpo.h) under SHADER_ROOT. Generated program data
# is never edited after compilation.
VCG_SOURCES := $(wildcard $(SRC_ROOT)/shaders/*.vcg)
FCG_SOURCES := $(wildcard $(SRC_ROOT)/shaders/*.fcg)
VPO_FILES := $(patsubst $(SRC_ROOT)/shaders/%.vcg,$(SHADER_ROOT)/%.vpo,$(VCG_SOURCES))
FPO_FILES := $(patsubst $(SRC_ROOT)/shaders/%.fcg,$(SHADER_ROOT)/%.fpo,$(FCG_SOURCES))
SHADER_OBJECTS := $(addsuffix .o,$(VPO_FILES)) $(addsuffix .o,$(FPO_FILES))
SHADER_HEADERS := $(patsubst %.vpo,%_vpo.h,$(VPO_FILES)) $(patsubst %.fpo,%_fpo.h,$(FPO_FILES))

INCLUDES := -I$(SRC_ROOT)/src -I$(SHADER_ROOT) -isystem $(PSL1GHT)/ppu/include
ifeq ($(MODE),Core)
# Project code compiled in Core mode must see the generated headers and must be built with
# the same language standard, exception and RTTI settings as the generated tree it links
# against, or the two halves disagree at the ABI boundary.
INCLUDES += $(GENERATED_SYSTEM_INCLUDES)
CXXFLAGS := $(GENERATED_STANDARD) -Wall -Wextra -Werror -O2 -g -mcpu=cell $(MACHDEP) -fexceptions -frtti $(NEWLIB_MATH_COMPAT) $(INCLUDES) $(HELENGINE_DEFINES)
else
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -O2 -g -mcpu=cell $(MACHDEP) $(INCLUDES) $(HELENGINE_DEFINES)
endif
LDFLAGS := $(MACHDEP) -Wl,-Map,$(MAP)
LIBS := -lrsx -lgcm_sys -lsysutil -lrt -llv2 -lm

.PHONY: all verify-generated-core
.DELETE_ON_ERROR:

ifeq ($(MODE),Core)
all: verify-generated-core $(ELF) $(SELF_OUT) $(MAP)
else
all: $(ELF) $(SELF_OUT) $(MAP)
endif

# Fail loudly if any generated source would be compiled twice, once through the unity
# translation unit and again as its own. Duplicate definitions would otherwise surface as
# confusing link errors far from their cause.
verify-generated-core:
ifeq ($(MODE),Core)
	@mkdir -p "$(BUILD_ROOT)"
	@printf '%s\n' $(addprefix $(GENERATED_CORE_ROOT)/,$(GENERATED_UNITY_INCLUDES)) | sort -u > "$(BUILD_ROOT)/generated-unity-includes.txt"
	@printf '%s\n' $(GENERATED_EXTRA_SOURCES) | sort -u > "$(BUILD_ROOT)/generated-extra-sources.txt"
	@comm -12 "$(BUILD_ROOT)/generated-unity-includes.txt" "$(BUILD_ROOT)/generated-extra-sources.txt" > "$(BUILD_ROOT)/generated-overlap.txt"
	@if [ -s "$(BUILD_ROOT)/generated-overlap.txt" ]; then \
	  echo "Generated sources would be compiled twice:"; cat "$(BUILD_ROOT)/generated-overlap.txt"; exit 1; \
	fi
	@echo "Generated Core: unity $(notdir $(GENERATED_UNITY_SOURCE)), $(words $(GENERATED_UNITY_INCLUDES)) included, $(words $(GENERATED_EXTRA_SOURCES)) separate translation units"
endif

$(OBJ_ROOT)/%.o: $(SRC_ROOT)/src/%.cpp $(SHADER_HEADERS)
	@mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

ifeq ($(MODE),Core)
$(GENERATED_UNITY_OBJECT): $(GENERATED_UNITY_SOURCE) $(SHADER_HEADERS)
	@mkdir -p "$(dir $@)"
	$(CXX) $(GENERATED_CXXFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"

$(OBJ_ROOT)/generated-extra/%.o: $(GENERATED_CORE_ROOT)/%.cpp $(SHADER_HEADERS)
	@mkdir -p "$(dir $@)"
	$(CXX) $(GENERATED_CXXFLAGS) -MMD -MP -MF "$(@:.o=.d)" -MT "$@" -c "$<" -o "$@"
endif

$(ELF): $(OBJECTS) $(SHADER_OBJECTS)
	@mkdir -p "$(dir $@)"
	$(CXX) $(OBJECTS) $(SHADER_OBJECTS) $(LDFLAGS) $(LIBPSL1GHT_LIB) $(LIBS) -o "$@"

$(MAP): $(ELF)

# SELF creation replicates the pinned ppu_rules %.self recipe (strip a staged
# copy, sprxlinker, make_self, fself) while keeping the unstripped ELF with
# symbols as the primary artifact.
$(SELF_OUT): $(ELF)
	@mkdir -p "$(SELF_STAGE)"
	$(STRIP) "$<" -o "$(SELF_STAGE)/$(TARGET).elf"
	$(SPRX) "$(SELF_STAGE)/$(TARGET).elf"
	$(SELF) "$(SELF_STAGE)/$(TARGET).elf" "$@"
	$(FSELF) "$(SELF_STAGE)/$(TARGET).elf" "$(FAKE_SELF_OUT)"

# Shader conversion: same commands as the pinned ppu_rules %.vpo/%.fpo recipes.
$(SHADER_ROOT)/%.vpo: $(SRC_ROOT)/shaders/%.vcg
	@mkdir -p "$(dir $@)"
	$(CGCOMP) -v $(CGCFLAGS) "$<" "$@"

$(SHADER_ROOT)/%.fpo: $(SRC_ROOT)/shaders/%.fcg
	@mkdir -p "$(dir $@)"
	$(CGCOMP) -f $(CGCFLAGS) "$<" "$@"

# Binary embedding: same as the pinned base_rules bin2o canned sequence, with the
# generated header written next to the object instead of the current directory.
# bin2s derives symbol names from the file name with '.' replaced by '_'.
define helengine_bin2o
	bin2s -a 64 "$<" | $(AS) -o "$@"
	@printf '/* Generated by the PS3 build from %s; do not edit. */\n' "$(notdir $<)" > "$(basename $<)_$(subst .,,$(suffix $<)).h"
	@printf 'extern const u8 %s[];\nextern const u32 %s_size;\n' "$(subst .,_,$(notdir $<))" "$(subst .,_,$(notdir $<))" >> "$(basename $<)_$(subst .,,$(suffix $<)).h"
endef

$(SHADER_ROOT)/%.vpo.o: $(SHADER_ROOT)/%.vpo
	$(helengine_bin2o)

$(SHADER_ROOT)/%.fpo.o: $(SHADER_ROOT)/%.fpo
	$(helengine_bin2o)

$(SHADER_ROOT)/%_vpo.h: $(SHADER_ROOT)/%.vpo.o
	@test -f "$@"

$(SHADER_ROOT)/%_fpo.h: $(SHADER_ROOT)/%.fpo.o
	@test -f "$@"

-include $(DEPENDS)
