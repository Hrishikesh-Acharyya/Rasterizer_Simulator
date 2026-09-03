# Build configuration
CXX      := g++

# Source layout. All translation units and headers live in SRC_DIR; TOOL_DIR
# holds standalone programs with their own main(), which must stay out of
# SOURCES or the link step gets two of them.
SRC_DIR  := src
TOOL_DIR := tools

# -I$(SRC_DIR) is what lets every #include "raster.h" stay exactly as it was
# after the move. Without it every include would need a relative path.
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I$(SRC_DIR)

# Output paths. Declared before the platform block below, which reads
# FRAME_DIR while building its commands.
FRAME_DIR := frames
VIDEO     := Media/Videos/testing.mp4

# Platform split. Windows itself sets $(OS) to Windows_NT, so this picks the
# shell and the file-removal commands with no action from the user.
# The Windows branch is the original one and must keep using cmd.exe: MinGW
# make would otherwise look for a POSIX shell that may not be installed.
ifeq ($(OS),Windows_NT)
    PCT          := %%
    SHELL        := cmd.exe
    .SHELLFLAGS  := /C
    TARGET       := renderer.exe
    DIFF_TARGET  := ppmdiff.exe
    RUN_TARGET   := $(TARGET)
    RUN_DIFF     := $(DIFF_TARGET)
    MKDIR_FRAMES := if not exist $(FRAME_DIR) mkdir $(FRAME_DIR)
    RM_FRAMES    := del /Q $(FRAME_DIR)\*.ppm 2>nul
    RM_OBJECTS   := del /Q *.o 2>nul
    RM_TARGET    := del /Q $(TARGET) 2>nul
    RM_DIFF      := del /Q $(DIFF_TARGET) 2>nul
else
    PCT          := %
    TARGET       := renderer
    DIFF_TARGET  := ppmdiff
    RUN_TARGET   := ./$(TARGET)
    RUN_DIFF     := ./$(DIFF_TARGET)
    MKDIR_FRAMES := mkdir -p $(FRAME_DIR)
    RM_FRAMES    := rm -f $(FRAME_DIR)/*.ppm
    RM_OBJECTS   := rm -f *.o
    RM_TARGET    := rm -f $(TARGET)
    RM_DIFF      := rm -f $(DIFF_TARGET)
endif

# Every .cpp in the RENDERER, named without a directory.
#
# The directory is supplied by the vpath below instead of being baked into
# these names, which keeps the .o files in the project root. Writing
# src/main.cpp here would make OBJECTS become src/main.o, scattering build
# products through the source tree where RM_OBJECTS would not find them.
SOURCES := main.cpp framebuffer.cpp raster.cpp raster_fixed.cpp model.cpp stats.cpp
OBJECTS := $(SOURCES:.cpp=.o)

# Where make looks for prerequisites it cannot find in the current directory.
vpath %.cpp $(SRC_DIR) $(TOOL_DIR)

# Coarse header dependency: any header change rebuilds every object. Imprecise
# but correct, and fine at this scale. wildcard rather than a hand-written list
# so a new header cannot be forgotten -- an omission there would silently give
# stale objects, which is the failure mode this rule exists to prevent.
HEADERS := $(wildcard $(SRC_DIR)/*.h)

# The first target is what plain `make` builds. Both programs, because the diff
# tool is how the renderer's output gets verified, and letting it go stale
# relative to the renderer is exactly the kind of thing that wastes an hour.
all: $(TARGET) $(DIFF_TARGET)

# Link step: needs every object file.
$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

# ppmdiff is standalone -- it includes nothing from this project, so it depends
# on its own source and nothing else, and is compiled straight to an executable
# without an intermediate .o. That also keeps it out of the %.o pattern rule and
# out of $(OBJECTS).
$(DIFF_TARGET): $(TOOL_DIR)/ppmdiff.cpp
	$(CXX) -std=c++17 -Wall -Wextra -O2 $< -o $@

# Pattern rule: how to make any .o from its .cpp, found via vpath.
# $< is the first prerequisite, $@ is the target.
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Build, clear stale frames, render.
# Deleting old frames matters: a leftover frame the new run does not overwrite
# will appear in the video and look like a rendering bug.
run: $(TARGET)
	@$(MKDIR_FRAMES)
	@$(RM_FRAMES)
	$(RUN_TARGET)

video: run
	ffmpeg -y -framerate 30 -i $(FRAME_DIR)/$(PCT)03d.ppm -c:v libx264 -pix_fmt yuv420p $(VIDEO)

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

# plot_sweep.py draws the sub-pixel sweep from the CSVs in stats/, so the figure
# cannot drift from the data it reports. Standard library only: regenerating a
# figure should not be what adds a dependency stack to a C++17 project.
graphs:
	python3 tools/plot_sweep.py
	dot -Tpng -Gdpi=140 $(GRAPH_LIGHT) $(GRAPH_DIR)/include_graph.dot -o $(GRAPH_DIR)/include_graph_light.png
	dot -Tpng -Gdpi=140 $(GRAPH_DARK)  $(GRAPH_DIR)/include_graph.dot -o $(GRAPH_DIR)/include_graph_dark.png
	dot -Tpng -Gdpi=140 $(GRAPH_LIGHT) $(GRAPH_DIR)/work_amplification.dot -o $(GRAPH_DIR)/work_amplification_light.png
	dot -Tpng -Gdpi=140 $(GRAPH_DARK)  $(GRAPH_DIR)/work_amplification.dot -o $(GRAPH_DIR)/work_amplification_dark.png

clean:
	@$(RM_OBJECTS)
	@$(RM_TARGET)
	@$(RM_DIFF)

# These are command names, not files to build. Without this, a file named
# "clean" in the directory would make `make clean` do nothing.
.PHONY: all run video docs graphs clean