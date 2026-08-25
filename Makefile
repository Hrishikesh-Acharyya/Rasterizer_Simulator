# Build configuration
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2

# Output paths. Declared before the platform block below, which reads
# FRAME_DIR while building its commands.
FRAME_DIR := frames
VIDEO     := Media/Videos/torus_gouraud_shading.mp4
GIF       := Media/Gifs/torus_gouraud_shading.gif

# Platform split. Windows itself sets $(OS) to Windows_NT, so this picks the
# shell and the file-removal commands with no action from the user.
# The Windows branch is the original one and must keep using cmd.exe: MinGW
# make would otherwise look for a POSIX shell that may not be installed.
ifeq ($(OS),Windows_NT)
    PCT   := %%
    SHELL := cmd.exe
    .SHELLFLAGS  := /C
    TARGET       := renderer.exe
    RUN_TARGET   := $(TARGET)
    MKDIR_FRAMES := if not exist $(FRAME_DIR) mkdir $(FRAME_DIR)
    RM_FRAMES    := del /Q $(FRAME_DIR)\*.ppm 2>nul
    RM_OBJECTS   := del /Q *.o 2>nul
    RM_TARGET    := del /Q $(TARGET) 2>nul
else
    PCT          := %
    TARGET       := renderer
    RUN_TARGET   := ./$(TARGET)
    MKDIR_FRAMES := mkdir -p $(FRAME_DIR)
    RM_FRAMES    := rm -f $(FRAME_DIR)/*.ppm
    RM_OBJECTS   := rm -f *.o
    RM_TARGET    := rm -f $(TARGET)
endif

# Every .cpp in the project, and the .o each produces
SOURCES := main.cpp framebuffer.cpp raster.cpp model.cpp
OBJECTS := $(SOURCES:.cpp=.o)

# Coarse header dependency: any header change rebuilds every object.
# Imprecise but correct, and fine at this scale.
HEADERS := vectors.h matrices.h types.h framebuffer.h raster.h model.h

# The first target is what plain `make` builds.
all: $(TARGET)

# Link step: needs every object file.
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

# Pattern rule: how to make any .o from its .cpp.
# $< is the first prerequisite, $@ is the target.
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Build, clear stale frames, render.
# Deleting old frames matters: a leftover frame the new run does not
# overwrite will appear in the video and look like a rendering bug.
run: $(TARGET)
	@$(MKDIR_FRAMES)
	@$(RM_FRAMES)
	$(RUN_TARGET)

video: run
	ffmpeg -y -framerate 30 -i $(FRAME_DIR)/$(PCT)03d.ppm -c:v libx264 -pix_fmt yuv420p $(VIDEO)

gif: run
	ffmpeg -y -i $(FRAME_DIR)/$(PCT)03d.ppm -vf "fps=15,scale=640:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse" $(GIF)

# Doxygen HTML built from the comments already in the headers. Optional:
# doxygen is not needed to build or run anything.
docs:
	doxygen Doxyfile

clean:
	@$(RM_OBJECTS)
	@$(RM_TARGET)

# These are command names, not files to build. Without this, a file named
# "clean" in the directory would make `make clean` do nothing.
.PHONY: all run video gif docs clean
