#pragma once
#include <string>
#include <array>

struct DisplayConfig {
    float gamma = 2.2f;
    int brightness = 300;
    float contrast = 1.0f;
    int res_w = 1920, res_h = 1080;
    int refresh = 60;
    float aspect_w = 16.0f, aspect_h = 9.0f;
    std::string orientation = "paysage";
    std::array<float,3> rgb = {1.0f, 1.0f, 1.0f};
    std::array<float,4> cmyk = {0.0f, 0.0f, 0.0f, 0.0f};
    float white = 1.0f;
    std::array<float,256> lut;
    int color_mode = 0;
    bool autostart = false;

    // ACES Display Settings
    bool aces_enabled = false;
    int aces_color_science = 0;        // 0 = ACEScct v1.3, 1 = ACEScc, 2 = ACEScg, 3 = ACES2065-1, 4 = DaVinci YRGB
    int aces_input_space = 0;          // 0 = Rec.709 / sRGB, 1 = REDWideGamutRGB, 2 = ARRI LogC3, 3 = Sony S-Gamut3.Cine, 4 = DCI-P3
    int aces_output_transform = 0;     // 0 = Rec.709 (Gamma 2.4), 1 = sRGB, 2 = DCI-P3 Cinema, 3 = Rec.2020 PQ (1000 nits), 4 = Rec.2020 HLG
    float aces_exposure = 0.0f;        // EV [-5.0, +5.0]
    float aces_color_temp = 6500.0f;   // Kelvin [2000K, 12000K]
    float aces_tint = 0.0f;            // [-50.0, +50.0]
    float aces_drx = 0.0f;             // Dynamic Range Extension [0.0, 1.0]
    float aces_shadow = 0.0f;          // Shadow Lift [-1.0, +1.0]
    float aces_brightness = 0.0f;      // Brightness Offset [-1.0, +1.0]

    // Chromatic Adaptation Transform (CAT) Settings
    int cat_method = 0;                // 0 = Bradford, 1 = CAT02, 2 = Von Kries, 3 = XYZ Scaling, 4 = Sharp CAT
    int cat_source_illuminant = 0;     // 0 = D65 (6504K), 1 = D50 (5003K), 2 = D55 (5500K), 3 = Illuminant A (2856K), 4 = Illuminant C (6774K), 5 = Illuminant E (5000K)
    int cat_target_illuminant = 0;     // 0 = D65, 1 = D50, 2 = D55, 3 = Illuminant A, 4 = Illuminant C, 5 = Illuminant E

    // Target Luminance Levels (cd/m²)
    float white_level_cdm2 = 120.0f;   // Niveau de blanc (cd/m²)
    float black_level_cdm2 = 0.0f;     // Niveau de noir (cd/m²)
};

void save_config(const DisplayConfig& cfg);
DisplayConfig load_config();
