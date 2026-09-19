CXX      ?= g++
# The same flags the sketch is built with. -ffp-contract=off -fno-fast-math is not a preference: it stops the
# compiler fusing multiply-adds and reassociating the expression, which is what lets the host and the board agree
# as far as IEEE allows. Drop them and the preview stops being a preview.
CXXFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter -ffp-contract=off -fno-fast-math
SRC      := preview.cpp \
            ../LiquidGlass/fg_tex.cpp \
            ../LiquidGlass/fg_shader.cpp \
            ../LiquidGlass/fg_render.cpp \
            ../LiquidGlass/fg_demo.cpp

preview: $(SRC) ../LiquidGlass/*.h png_write.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lm

# Upstream's sampling, exactly: eight spectral taps and a lerp between mip levels. Slower, and bit-identical to
# the shader's own quadrature — the shipped defaults trade 0.108/255 for 1.29-1.46x. See arduino/README.md.
exact: $(SRC) ../LiquidGlass/*.h png_write.h
	$(CXX) $(CXXFLAGS) -DFG_SPECTRAL_TAPS=8 -DFG_TRILINEAR=1 -o preview_exact $(SRC) -lm

# The WebGL build's corner and ambient instead of the desktop build's. This is the configuration the fidelity
# numbers in the README were measured in: mean |diff| 0.245/255 against the real WebGL2 renderer.
webgl: $(SRC) ../LiquidGlass/*.h png_write.h
	$(CXX) $(CXXFLAGS) -DFG_CORNERS_CONTINUOUS=0 -DFG_AMBIENT_HUE=0 -o preview_webgl $(SRC) -lm

run: preview
	./preview

clean:
	rm -f preview preview_exact preview_webgl preview*.png anim*.png

.PHONY: run clean exact webgl
