#pragma once
#include <string>
#include <vector>

std::string exec_cmd(const char* cmd);
bool file_exists(const std::string& path);
std::string get_output_name();

// Conversions d'espaces colorimétriques (RVB, HSL, HSV, HSI)
void rgb_to_hsl(float r, float g, float b, float& h, float& s, float& l);
void hsl_to_rgb(float h, float s, float l, float& r, float& g, float& b);

void rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v);
void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b);

void rgb_to_hsi(float r, float g, float b, float& h, float& s, float& i);
void hsi_to_rgb(float h, float s, float i, float& r, float& g, float& b);

