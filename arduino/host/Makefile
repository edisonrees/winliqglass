CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter -I../LiquidGlass
SRC      := preview.cpp \
            ../LiquidGlass/lg_field.cpp \
            ../LiquidGlass/lg_bg.cpp \
            ../LiquidGlass/lg_rays.cpp \
            ../LiquidGlass/lg_render.cpp \
            ../LiquidGlass/lg_demo.cpp

preview: $(SRC) ../LiquidGlass/*.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

# Same sources at the Uno budget, to prove the small-tier config still builds.
tiny: $(SRC) ../LiquidGlass/*.h
	$(CXX) $(CXXFLAGS) -DLG_TIER=0 -o preview_tiny $(SRC)

run: preview
	./preview

clean:
	rm -f preview preview_tiny preview*.png anim*.png

.PHONY: run clean tiny
