#version 450

// UTA-0019 SS 4.8's stand-in shapes: one flat colour, unlit. The colour was
// divided by the exposure on the host (Frame.cpp's setShapes note), so the
// output stage shows it as given; alpha blends it over the level.

layout(location = 0) flat in vec4 colour;

layout(location = 0) out vec4 outColour;

void main() {
    outColour = colour;
}
