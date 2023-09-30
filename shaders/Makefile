SPV_FILES = $(wildcard */*.spv)

VERT_SOURCES = $(filter-out $(SPV_FILES), $(wildcard */*.vert))
FRAG_SOURCES = $(filter-out $(SPV_FILES), $(wildcard */*.frag))
COMP_SOURCES = $(filter-out $(SPV_FILES), $(wildcard */*.comp))
GLSL_INCLUDES = $(wildcard */*.glsl)

VERT_SPV = $(VERT_SOURCES:%.vert=%.vert.spv)
FRAG_SPV = $(FRAG_SOURCES:%.frag=%.frag.spv)
COMP_SPV = $(COMP_SOURCES:%.comp=%.comp.spv)


GLSLC_FLAGS = -g --target-env=vulkan1.2

all : $(VERT_SPV) $(FRAG_SPV) $(COMP_SPV)

$(VERT_SPV) : $(VERT_SOURCES) $(GLSL_INCLUDES)
	glslc $(GLSLC_FLAGS) $(@:%.vert.spv=%.vert) -o $@

$(FRAG_SPV) : $(FRAG_SOURCES) $(GLSL_INCLUDES)
	glslc $(GLSLC_FLAGS) $(@:%.frag.spv=%.frag) -o $@

$(COMP_SPV) : $(COMP_SOURCES) $(GLSL_INCLUDES)
	glslc $(GLSLC_FLAGS) $(@:%.comp.spv=%.comp) -o $@