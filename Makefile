PCT := %%
SHELL := cmd.exe
.SHELLFLAGS := /C
# Build configuration
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET := renderer.exe

# Every .cpp in the project, and the .o each produces
SOURCES := main.cpp framebuffer.cpp raster.cpp model.cpp
OBJECTS := $(SOURCES:.cpp=.o)

# Coarse header dependency: any header change rebuilds every object.
# Imprecise but correct, and fine at this scale.
HEADERS := vectors.h matrices.h types.h framebuffer.h raster.h model.h

# Output paths
FRAME_DIR := frames
VIDEO     := Media/Videos/torus_gouraud_shading.mp4
GIF       := Media/Gifs/torus_gouraud_shading.gif

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
	@if not exist $(FRAME_DIR) mkdir $(FRAME_DIR)
	@del /Q $(FRAME_DIR)\*.ppm 2>nul
	$(TARGET)

video: run
	ffmpeg -y -framerate 30 -i $(FRAME_DIR)/$(PCT)03d.ppm -c:v libx264 -pix_fmt yuv420p $(VIDEO)

gif: run
	ffmpeg -y -i $(FRAME_DIR)/$(PCT)03d.ppm -vf "fps=15,scale=640:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse" $(GIF)

clean:
	@del /Q *.o 2>nul
	@del /Q $(TARGET) 2>nul

# These are command names, not files to build. Without this, a file named
# "clean" in the directory would make `make clean` do nothing.
.PHONY: all run video gif clean