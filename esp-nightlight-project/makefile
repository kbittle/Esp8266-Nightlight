# Get the short Git commit hash (fallback to 'nogit' if git isn't available)
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo "nogit")

# Dynamic project name incorporating the hash
PROJECT_NAME := esp-nightlight-project-$(GIT_HASH)

include $(IDF_PATH)/make/project.mk

# Override the clean target to wipe the entire build directory
clean:
	@echo "Removing build directory..."
	rm -rf $(BUILD_DIR_BASE)
