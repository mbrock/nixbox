// The probe's report: lines drawn on screen and written to LocalState.
#pragma once

#include <d3d12.h>

#include <string>
#include <vector>

extern std::vector<std::string> lines;

void line(const char *format, ...);

// Formats, multisampling, and the rest of the format-dependent support a
// renderer chooses its targets from.
void describe_formats(ID3D12Device *device);

// Runs small GPU programs that exercise the binding, shader, indirect, and
// render-target patterns a renderer would build on, then times throughput.
void run_gpu_tests(ID3D12Device *device);

// What the console's display and the DXGI outputs say about resolution,
// refresh rate, and HDR.
void describe_display();
