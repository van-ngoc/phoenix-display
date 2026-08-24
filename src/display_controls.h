#pragma once
#include "config.h"
#include <string>

#include <vector>

struct ToneCurvePoint {
    float x; // [0.0, 1.0]
    float y; // [0.0, 1.0]
};

extern DisplayConfig g_config;

void init_display_controls();
void render_settings();

void apply_gamma(float gamma);
void apply_brightness(int lux);
void apply_contrast(float contrast);
void apply_resolution(int w, int h);
void apply_refresh(int hz);
void apply_aspect_ratio(float w, float h);
void apply_orientation(const std::string& mode);
void apply_lut(const float* curve);
void apply_color_adjust(float r, float g, float b, float c, float m, float y, float w, float k);
void apply_color_mode(int mode);
void apply_shadows_highlights(float shadows, float highlights);
void apply_autostart(bool enabled);
void edit_lut_curve();
void render_aces_controls();
void apply_aces_settings();
void render_cat_controls();
void apply_chromatic_adaptation();
void apply_luminance_levels(float white_cdm2, float black_cdm2);
void reset_all_display_settings();
void save_current_config();

