# Build configuration
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2

# Output paths. Declared before the platform block below, which reads
# FRAME_DIR while building its commands.
FRAME_DIR := frames
VIDEO     := Media/Videos/torus_gouraud_shading_perspective_corrected.mp4
GIF       := Media/Gifs/torus_gouraud_shading_perspective_corrected.gif

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

# Graph images for the README, drawn from the .dot sources in Media/Graphs.
# Two variants per graph so the README can serve the right one for the reader's
# GitHub theme. The colours are passed on the command line rather than written
# into the .dot files, so one source produces both.
#
# The backslashes before each # are required: an unescaped # starts a comment
# in a Makefile and would truncate the colour.
GRAPH_LIGHT := -Gbgcolor=transparent -Nfillcolor=\#eef2f7 -Ncolor=\#4a5568 \
               -Nfontcolor=\#1a202c -Ecolor=\#5a6a7d -Efontcolor=\#3d4a5c \
               -Gfontcolor=\#3d4a5c -Gcolor=\#94a3b8
GRAPH_DARK  := -Gbgcolor=transparent -Nfillcolor=\#2c3440 -Ncolor=\#8fa3bd \
               -Nfontcolor=\#e8eef6 -Ecolor=\#93a7c0 -Efontcolor=\#c2d0e0 \
               -Gfontcolor=\#c2d0e0 -Gcolor=\#7b8da3
GRAPH_DIR   := Media/Graphs

graphs:
	dot -Tpng -Gdpi=140 $(GRAPH_LIGHT) $(GRAPH_DIR)/include_graph.dot -o $(GRAPH_DIR)/include_graph_light.png
	dot -Tpng -Gdpi=140 $(GRAPH_DARK)  $(GRAPH_DIR)/include_graph.dot -o $(GRAPH_DIR)/include_graph_dark.png
	dot -Tpng -Gdpi=140 $(GRAPH_LIGHT) $(GRAPH_DIR)/work_amplification.dot -o $(GRAPH_DIR)/work_amplification_light.png
	dot -Tpng -Gdpi=140 $(GRAPH_DARK)  $(GRAPH_DIR)/work_amplification.dot -o $(GRAPH_DIR)/work_amplification_dark.png

clean:
	@$(RM_OBJECTS)
	@$(RM_TARGET)

# These are command names, not files to build. Without this, a file named
# "clean" in the directory would make `make clean` do nothing.
.PHONY: all run video gif docs graphs clean
