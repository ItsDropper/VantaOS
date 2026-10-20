# GPU source ownership is split by stable architectural boundary.
include drivers/gpu/core/Makefile
include drivers/gpu/intel/Makefile
include drivers/gpu/amd/Makefile
include drivers/gpu/display/Makefile
include drivers/gpu/nvx/Makefile

VANTAOS_GPU_C := $(VANTAOS_GPU_CORE_C) $(VANTAOS_GPU_DISPLAY_C) $(VANTAOS_GPU_INTEL_C) $(VANTAOS_GPU_AMD_C) $(VANTAOS_NVX_C)
