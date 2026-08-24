#!/bin/bash
# create_sources.sh - Crée tous les fichiers sources dans src/

mkdir -p src

cat > src/utils.h << 'EOF'
#pragma once
#include <string>
#include <vector>

std::string exec_cmd(const char* cmd);
bool file_exists(const std::string& path);
std::string get_output_name();
EOF

cat > src/utils.cpp << 'EOF'
#include "utils.h"
#include <cstdio>
#include <memory>
#include <array>
#include <fstream>

std::string exec_cmd(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

bool file_exists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

std::string get_output_name() {
    std::string output = exec_cmd("xrandr | grep ' connected' | head -1 | cut -d' ' -f1");
    if (!output.empty() && output.back() == '\n') output.pop_back();
    return output.empty() ? "eDP-1" : output;
}
EOF

cat > src/config.h << 'EOF'
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
};

void save_config(const DisplayConfig& cfg);
DisplayConfig load_config();
EOF

cat > src/config.cpp << 'EOF'
#include "config.h"
#include <json/json.h>
#include <fstream>
#include <iostream>

static const char* CONFIG_FILE = "phoenix_config.json";

void save_config(const DisplayConfig& cfg) {
    Json::Value root;
    root["gamma"] = cfg.gamma;
    root["brightness"] = cfg.brightness;
    root["contrast"] = cfg.contrast;
    root["res_w"] = cfg.res_w;
    root["res_h"] = cfg.res_h;
    root["refresh"] = cfg.refresh;
    root["aspect_w"] = cfg.aspect_w;
    root["aspect_h"] = cfg.aspect_h;
    root["orientation"] = cfg.orientation;
    for (int i = 0; i < 3; ++i) root["rgb"][i] = cfg.rgb[i];
    for (int i = 0; i < 4; ++i) root["cmyk"][i] = cfg.cmyk[i];
    root["white"] = cfg.white;
    for (int i = 0; i < 256; ++i) root["lut"][i] = cfg.lut[i];
    root["color_mode"] = cfg.color_mode;

    std::ofstream file(CONFIG_FILE);
    if (file.is_open()) file << root.toStyledString();
    else std::cerr << "Erreur: impossible d'écrire " << CONFIG_FILE << std::endl;
}

DisplayConfig load_config() {
    DisplayConfig cfg;
    for (int i = 0; i < 256; ++i) cfg.lut[i] = i / 255.0f;
    std::ifstream file(CONFIG_FILE);
    if (!file.is_open()) return cfg;
    Json::Value root;
    file >> root;

    cfg.gamma = root.get("gamma", 2.2f).asFloat();
    cfg.brightness = root.get("brightness", 300).asInt();
    cfg.contrast = root.get("contrast", 1.0f).asFloat();
    cfg.res_w = root.get("res_w", 1920).asInt();
    cfg.res_h = root.get("res_h", 1080).asInt();
    cfg.refresh = root.get("refresh", 60).asInt();
    cfg.aspect_w = root.get("aspect_w", 16.0f).asFloat();
    cfg.aspect_h = root.get("aspect_h", 9.0f).asFloat();
    cfg.orientation = root.get("orientation", "paysage").asString();
    if (root.isMember("rgb") && root["rgb"].isArray() && root["rgb"].size() >= 3) {
        for (int i=0;i<3;++i) cfg.rgb[i] = root["rgb"][i].asFloat();
    }
    if (root.isMember("cmyk") && root["cmyk"].isArray() && root["cmyk"].size() >= 4) {
        for (int i=0;i<4;++i) cfg.cmyk[i] = root["cmyk"][i].asFloat();
    }
    cfg.white = root.get("white", 1.0f).asFloat();
    if (root.isMember("lut") && root["lut"].isArray() && root["lut"].size() >= 256) {
        for (int i=0;i<256;++i) cfg.lut[i] = root["lut"][i].asFloat();
    }
    cfg.color_mode = root.get("color_mode", 0).asInt();
    return cfg;
}
EOF

cat > src/progress_bars.h << 'EOF'
#pragma once
#include <atomic>

extern std::atomic<float> g_download_progress;
extern std::atomic<float> g_processing_progress;
extern std::atomic<float> g_compute_progress;
extern std::atomic<float> g_ai_progress;

void init_progress_bars();
void render_progress_bars();

inline void set_download_progress(float v) { g_download_progress = v; }
inline void set_processing_progress(float v) { g_processing_progress = v; }
inline void set_compute_progress(float v) { g_compute_progress = v; }
inline void set_ai_progress(float v) { g_ai_progress = v; }
EOF

cat > src/progress_bars.cpp << 'EOF'
#include "progress_bars.h"
#include <imgui.h>

std::atomic<float> g_download_progress{0.0f};
std::atomic<float> g_processing_progress{0.0f};
std::atomic<float> g_compute_progress{0.0f};
std::atomic<float> g_ai_progress{0.0f};

void init_progress_bars() {}
void render_progress_bars() {
    ImGui::Separator();
    ImGui::Text("Téléchargement");
    ImGui::ProgressBar(g_download_progress.load(), ImVec2(-1, 0));
    ImGui::Text("Traitement");
    ImGui::ProgressBar(g_processing_progress.load(), ImVec2(-1, 0));
    ImGui::Text("Calcul intensif");
    ImGui::ProgressBar(g_compute_progress.load(), ImVec2(-1, 0));
    ImGui::Text("Génération AI");
    ImGui::ProgressBar(g_ai_progress.load(), ImVec2(-1, 0));
}
EOF

cat > src/display_controls.h << 'EOF'
#pragma once
#include "config.h"
#include <string>

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
void edit_lut_curve();
void save_current_config();
EOF

cat > src/display_controls.cpp << 'EOF'
#include "display_controls.h"
#include "utils.h"
#include "config.h"
#include <imgui.h>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cstdio>

#ifdef HAVE_GPS
#include <gps.h>
#endif

DisplayConfig g_config;
static std::string g_output_name;
static float lut_curve[256];
static bool lut_dirty = false;

static void run_cmd(const std::string& cmd) {
    int ret = system(cmd.c_str());
    (void)ret; // ignore
}

void init_display_controls() {
    g_output_name = get_output_name();
    g_config = load_config();
    for (int i=0;i<256;++i) lut_curve[i] = g_config.lut[i];
    apply_gamma(g_config.gamma);
    apply_brightness(g_config.brightness);
    apply_contrast(g_config.contrast);
    apply_resolution(g_config.res_w, g_config.res_h);
    apply_refresh(g_config.refresh);
    apply_aspect_ratio(g_config.aspect_w, g_config.aspect_h);
    apply_orientation(g_config.orientation);
    apply_color_adjust(g_config.rgb[0], g_config.rgb[1], g_config.rgb[2],
                       g_config.cmyk[0], g_config.cmyk[1], g_config.cmyk[2],
                       g_config.white, g_config.cmyk[3]);
    apply_lut(lut_curve);
    apply_color_mode(g_config.color_mode);
}

void apply_gamma(float gamma) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "xgamma -gamma %.2f 2>/dev/null", gamma);
    if (system(cmd) != 0) {
        snprintf(cmd, sizeof(cmd), "xrandr --output %s --gamma %.2f:%.2f:%.2f",
                 g_output_name.c_str(), gamma, gamma, gamma);
        run_cmd(cmd);
    }
}

void apply_brightness(int lux) {
    float b = std::clamp(lux / 1000.0f, 0.1f, 1.0f);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "xrandr --output %s --brightness %.2f", g_output_name.c_str(), b);
    run_cmd(cmd);
}

void apply_contrast(float contrast) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "xrandr --output %s --gamma %.2f:%.2f:%.2f",
             g_output_name.c_str(), contrast, contrast, contrast);
    run_cmd(cmd);
}

void apply_resolution(int w, int h) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "xrandr --output %s --mode %dx%d", g_output_name.c_str(), w, h);
    run_cmd(cmd);
}

void apply_refresh(int hz) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "xrandr --output %s --rate %d", g_output_name.c_str(), hz);
    run_cmd(cmd);
}

void apply_aspect_ratio(float w, float h) {
    int new_w = (int)(g_config.res_h * w / h);
    apply_resolution(new_w, g_config.res_h);
}

void apply_orientation(const std::string& mode) {
    std::string cmd = "xrandr --output " + g_output_name + " --rotate ";
    if (mode == "portrait") cmd += "left";
    else if (mode == "paysage") cmd += "normal";
    else return;
    run_cmd(cmd);
}

void apply_lut(const float* curve) {
    for (int i=0;i<256;++i) g_config.lut[i] = curve[i];
    lut_dirty = false;
    // on pourrait appeler xcalib mais on se contente de sauvegarder
}

void apply_color_adjust(float r, float g, float b, float c, float m, float y, float w, float k) {
    float r_eff = r * (1.0f - c) * (1.0f - m) * (1.0f - k) * w;
    float g_eff = g * (1.0f - c) * (1.0f - y) * (1.0f - k) * w;
    float b_eff = b * (1.0f - m) * (1.0f - y) * (1.0f - k) * w;
    r_eff = std::clamp(r_eff, 0.1f, 2.0f);
    g_eff = std::clamp(g_eff, 0.1f, 2.0f);
    b_eff = std::clamp(b_eff, 0.1f, 2.0f);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "xrandr --output %s --gamma %.2f:%.2f:%.2f",
             g_output_name.c_str(), r_eff, g_eff, b_eff);
    run_cmd(cmd);
}

void apply_color_mode(int mode) {
    if (mode == 0) {
        run_cmd("xrandr --output " + g_output_name + " --set \"Broadcast RGB\" \"Full\"");
    } else if (mode == 1) {
        run_cmd("xrandr --output " + g_output_name + " --set \"Broadcast RGB\" \"Full\"");
        // DCI-P3 simulé
    }
}

void apply_shadows_highlights(float shadows, float highlights) {
    for (int i=0;i<256;++i) {
        float t = i / 255.0f;
        float shadow_factor = 1.0f + shadows * 0.5f;
        float val = powf(t, 1.0f / shadow_factor);
        float hl_factor = 1.0f + (highlights - 0.5f) * 0.5f;
        val = powf(val, hl_factor);
        lut_curve[i] = std::clamp(val, 0.0f, 1.0f);
    }
    apply_lut(lut_curve);
}

void edit_lut_curve() {
    ImGui::Text("Cliquez sur la courbe pour modifier les points");
    ImGui::PlotLines("##LUT_editor", lut_curve, 256, 0, nullptr, 0.0f, 1.0f, ImVec2(400, 150));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDown(0)) {
        ImVec2 pos = ImGui::GetMousePos();
        ImVec2 rect_min = ImGui::GetItemRectMin();
        ImVec2 rect_max = ImGui::GetItemRectMax();
        float t = (pos.x - rect_min.x) / (rect_max.x - rect_min.x);
        float val = 1.0f - (pos.y - rect_min.y) / (rect_max.y - rect_min.y);
        int idx = (int)(t * 255);
        if (idx >= 0 && idx < 256) {
            lut_curve[idx] = std::clamp(val, 0.0f, 1.0f);
            lut_dirty = true;
        }
    }
    if (ImGui::Button("Appliquer LUT")) {
        apply_lut(lut_curve);
    }
    ImGui::SameLine();
    if (ImGui::Button("Réinitialiser LUT")) {
        for (int i=0;i<256;++i) lut_curve[i] = i / 255.0f;
        apply_lut(lut_curve);
    }
}

void save_current_config() {
    save_config(g_config);
}

void render_settings() {
    ImGui::BeginChild("Settings", ImVec2(0, -50), true);

    static int brightness = g_config.brightness;
    ImGui::SliderInt("Luminance (lux)", &brightness, 0, 1000);
    if (ImGui::Button("Appliquer Luminance")) {
        g_config.brightness = brightness;
        apply_brightness(brightness);
    }

    static float rgb[3] = {g_config.rgb[0], g_config.rgb[1], g_config.rgb[2]};
    static float cmyk[4] = {g_config.cmyk[0], g_config.cmyk[1], g_config.cmyk[2], g_config.cmyk[3]};
    static float white = g_config.white;
    ImGui::ColorEdit3("RGB", rgb);
    ImGui::SliderFloat("Cyan", &cmyk[0], 0, 1);
    ImGui::SliderFloat("Magenta", &cmyk[1], 0, 1);
    ImGui::SliderFloat("Jaune", &cmyk[2], 0, 1);
    ImGui::SliderFloat("Noir", &cmyk[3], 0, 1);
    ImGui::SliderFloat("Blanc", &white, 0, 2);
    if (ImGui::Button("Appliquer Couleurs")) {
        for (int i=0;i<3;++i) g_config.rgb[i] = rgb[i];
        for (int i=0;i<4;++i) g_config.cmyk[i] = cmyk[i];
        g_config.white = white;
        apply_color_adjust(rgb[0], rgb[1], rgb[2], cmyk[0], cmyk[1], cmyk[2], white, cmyk[3]);
    }

    edit_lut_curve();

    static int gamma_idx = 3;
    const char* gamma_items[] = {"1.6","1.8","2.0","2.2","2.4","2.6"};
    ImGui::Combo("Gamma", &gamma_idx, gamma_items, IM_ARRAYSIZE(gamma_items));
    if (ImGui::Button("Appliquer Gamma")) {
        float g = atof(gamma_items[gamma_idx]);
        g_config.gamma = g;
        apply_gamma(g);
    }

    static float contrast = g_config.contrast;
    ImGui::SliderFloat("Contraste", &contrast, 0.1f, 2.0f);
    if (ImGui::Button("Appliquer Contraste")) {
        g_config.contrast = contrast;
        apply_contrast(contrast);
    }

    static float shadows = 0.0f, highlights = 1.0f;
    ImGui::SliderFloat("Ombres (courbe bas)", &shadows, 0.0f, 1.0f);
    ImGui::SliderFloat("Hautes lumières (courbe haut)", &highlights, 0.0f, 1.0f);
    if (ImGui::Button("Appliquer ombres/hautes lumières")) {
        apply_shadows_highlights(shadows, highlights);
    }

#ifdef HAVE_GPS
    struct gps_data_t gps_data;
    if (gps_open("localhost", "2947", &gps_data) == 0) {
        if (gps_read(&gps_data) > 0 && gps_data.status == STATUS_FIX) {
            ImGui::Text("GPS: %.6f°N, %.6f°E", gps_data.fix.latitude, gps_data.fix.longitude);
        } else {
            ImGui::Text("GPS: en attente de fix...");
        }
        gps_close(&gps_data);
    } else {
        ImGui::Text("GPS: service non disponible");
    }
#else
    ImGui::Text("GPS: support désactivé (libgps non trouvée)");
#endif

    static int res_idx = 0;
    const char* resolutions[] = {"1920x1080","1600x900","1280x720","1024x768"};
    ImGui::Combo("Résolution", &res_idx, resolutions, IM_ARRAYSIZE(resolutions));
    if (ImGui::Button("Appliquer Résolution")) {
        int w,h;
        sscanf(resolutions[res_idx], "%dx%d", &w, &h);
        g_config.res_w = w; g_config.res_h = h;
        apply_resolution(w,h);
    }

    static int refresh_idx = 0;
    const char* refresh_rates[] = {"60","75","120","144"};
    ImGui::Combo("Rafraîchissement (Hz)", &refresh_idx, refresh_rates, IM_ARRAYSIZE(refresh_rates));
    if (ImGui::Button("Appliquer Rafraîchissement")) {
        int hz = atoi(refresh_rates[refresh_idx]);
        g_config.refresh = hz;
        apply_refresh(hz);
    }

    static int aspect_idx = 0;
    const char* aspect_ratios[] = {"16:9","16:10","4:3","21:9","1:1","9:16","2.39:1"};
    float aspect_vals[][2] = {{16,9},{16,10},{4,3},{21,9},{1,1},{9,16},{2.39f,1}};
    ImGui::Combo("Ratio d'aspect", &aspect_idx, aspect_ratios, IM_ARRAYSIZE(aspect_ratios));
    if (ImGui::Button("Appliquer Ratio")) {
        g_config.aspect_w = aspect_vals[aspect_idx][0];
        g_config.aspect_h = aspect_vals[aspect_idx][1];
        apply_aspect_ratio(g_config.aspect_w, g_config.aspect_h);
    }

    static int orient_idx = 0;
    const char* orientations[] = {"Paysage","Portrait"};
    ImGui::Combo("Orientation", &orient_idx, orientations, IM_ARRAYSIZE(orientations));
    if (ImGui::Button("Appliquer Orientation")) {
        g_config.orientation = orientations[orient_idx];
        apply_orientation(g_config.orientation);
    }

    static int color_mode = g_config.color_mode;
    const char* color_modes[] = {"sRGB","DCI-P3"};
    ImGui::Combo("Mode couleur", &color_mode, color_modes, IM_ARRAYSIZE(color_modes));
    if (ImGui::Button("Appliquer Mode")) {
        g_config.color_mode = color_mode;
        apply_color_mode(color_mode);
    }

    if (ImGui::Button("Sauvegarder la configuration")) {
        save_current_config();
    }

    ImGui::EndChild();
}
EOF

cat > src/test_patterns.h << 'EOF'
#pragma once
void init_test_patterns();
void render_tests();
void draw_test(int test_id);
EOF

cat > src/test_patterns.cpp << 'EOF'
#include "test_patterns.h"
#include <imgui.h>
#include <GL/gl.h>
#include <vector>
#include <cmath>

static GLuint test_texture = 0;
static int tex_width = 512, tex_height = 512;
static bool test_active = false;
static int current_test = 0;

const char* test_names[] = {
    "Ghosting", "Couleurs", "Saut d'image",
    "Uniformité", "Luminance - Détails ombrelle",
    "Luminance - Dégradé", "Luminance - Échelons gris",
    "Contraste - Détails hautes lumières",
    "Contraste - Gradient", "Contraste - Plage dynamique"
};

static void generate_test_pattern(int id, std::vector<unsigned char>& pixels) {
    for (int y=0; y<tex_height; ++y) {
        for (int x=0; x<tex_width; ++x) {
            int idx = (y*tex_width + x)*3;
            float fx = (float)x/tex_width;
            float fy = (float)y/tex_height;
            unsigned char r=0,g=0,b=0;
            switch(id) {
                case 0: { bool grid = ((int)(fx*20)%2==0) ^ ((int)(fy*20)%2==0); r=grid?255:0; g=grid?0:255; b=0; break; }
                case 1: { if(fx<0.33f){r=255;} else if(fx<0.66f){g=255;} else {b=255;} break; }
                case 2: { bool ch = ((int)(fx*8)%2==0) ^ ((int)(fy*8)%2==0); r=g=b=ch?255:0; break; }
                case 3: { float v = 128+64*sinf(fx*20)*cosf(fy*20); r=g=b=(unsigned char)v; break; }
                case 4: { float d = sqrtf((fx-0.5f)*(fx-0.5f)+(fy-0.5f)*(fy-0.5f))*2; float v=255*(1-d); r=g=b=(unsigned char)std::clamp(v,0.f,255.f); break; }
                case 5: { r=g=b=(unsigned char)(fx*255); break; }
                case 6: { int steps=16; int val=(int)(fx*steps)*(255/(steps-1)); r=g=b=(unsigned char)val; break; }
                case 7: { float v=200+50*sinf(fx*50)*cosf(fy*50); r=g=b=(unsigned char)std::clamp(v,0.f,255.f); break; }
                case 8: { float v=128+127*sinf(fx*10); r=g=b=(unsigned char)v; break; }
                case 9: { float v=0; if(fx<0.1f) v=0; else if(fx<0.2f) v=20; else if(fx<0.3f) v=40; else if(fx<0.4f) v=80; else if(fx<0.5f) v=120; else if(fx<0.6f) v=160; else if(fx<0.7f) v=200; else if(fx<0.8f) v=220; else if(fx<0.9f) v=240; else v=255; r=g=b=(unsigned char)v; break; }
                default: break;
            }
            pixels[idx]=r; pixels[idx+1]=g; pixels[idx+2]=b;
        }
    }
}

void init_test_patterns() {
    glGenTextures(1, &test_texture);
    glBindTexture(GL_TEXTURE_2D, test_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tex_width, tex_height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    std::vector<unsigned char> pixels(tex_width*tex_height*3, 128);
    generate_test_pattern(0, pixels);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_width, tex_height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
}

void draw_test(int test_id) {
    if(!test_active) return;
    std::vector<unsigned char> pixels(tex_width*tex_height*3);
    generate_test_pattern(test_id, pixels);
    glBindTexture(GL_TEXTURE_2D, test_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_width, tex_height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    ImGui::Image((void*)(intptr_t)test_texture, ImVec2(400,400));
}

void render_tests() {
    ImGui::BeginChild("Tests", ImVec2(0,-50), true);
    ImGui::Combo("Sélectionner un test", &current_test, test_names, IM_ARRAYSIZE(test_names));
    if(ImGui::Button("Lancer le test")) test_active = true;
    ImGui::SameLine();
    if(ImGui::Button("Arrêter")) test_active = false;
    if(test_active) {
        ImGui::Text("Test en cours : %s", test_names[current_test]);
        draw_test(current_test);
    } else {
        ImGui::Text("Aucun test actif.");
    }
    ImGui::EndChild();
}
EOF

cat > src/ai_integration.h << 'EOF'
#pragma once
void init_ai_integration();
void render_ai();
void query_rag(const char* question);
void cowork_share(const char* data);
void openclaw_execute(const char* command);
void fetch_internet(const char* url);
EOF

cat > src/ai_integration.cpp << 'EOF'
#include "ai_integration.h"
#include "progress_bars.h"
#include <imgui.h>
#include <curl/curl.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>
#include <iostream>

static char question_buf[256] = "";
static char cowork_buf[256] = "";
static char openclaw_buf[256] = "";
static char url_buf[256] = "https://httpbin.org/get";
static char response_text[4096] = "";
static std::atomic<bool> rag_running{false};
static std::atomic<bool> fetch_running{false};

size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size*nmemb;
    char* dest = (char*)userp;
    strncat(dest, (char*)contents, total);
    return total;
}

void init_ai_integration() {
    curl_global_init(CURL_GLOBAL_ALL);
    response_text[0]='\0';
}

void query_rag(const char* question) {
    if(rag_running) return;
    rag_running = true;
    set_ai_progress(0.0f);
    std::thread([question](){
        for(int i=0;i<=10;++i){ std::this_thread::sleep_for(std::chrono::milliseconds(300)); set_ai_progress(i/10.0f); }
        snprintf(response_text, sizeof(response_text), "Réponse RAG à : %s\n(Simulation)", question);
        rag_running = false;
        set_ai_progress(1.0f);
    }).detach();
}

void cowork_share(const char* data) {
    snprintf(response_text, sizeof(response_text), "Données partagées avec cowork : %s", data);
    set_processing_progress(0.5f);
    std::thread([](){
        for(int i=0;i<=10;++i){ std::this_thread::sleep_for(std::chrono::milliseconds(100)); set_processing_progress(i/10.0f); }
    }).detach();
}

void openclaw_execute(const char* command) {
    int ret = system(command);
    snprintf(response_text, sizeof(response_text), "Commande exécutée (code %d) : %s", ret, command);
    set_compute_progress(1.0f);
}

void fetch_internet(const char* url) {
    if(fetch_running) return;
    fetch_running = true;
    response_text[0]='\0';
    set_download_progress(0.0f);
    std::thread([url](){
        CURL* curl = curl_easy_init();
        if(curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, response_text);
            for(int i=0;i<=10;++i){ std::this_thread::sleep_for(std::chrono::milliseconds(200)); set_download_progress(i/10.0f); }
            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            if(res != CURLE_OK)
                snprintf(response_text, sizeof(response_text), "Erreur curl: %s", curl_easy_strerror(res));
        } else {
            snprintf(response_text, sizeof(response_text), "Impossible d'initialiser curl");
        }
        fetch_running = false;
        set_download_progress(1.0f);
    }).detach();
}

void render_ai() {
    ImGui::BeginChild("AI", ImVec2(0,-50), true);
    ImGui::InputText("Question RAG", question_buf, sizeof(question_buf));
    if(ImGui::Button("Interroger RAG") && !rag_running) {
        query_rag(question_buf);
    }
    ImGui::SameLine();
    if(rag_running) ImGui::Text("...en cours");

    ImGui::InputText("Partager avec cowork", cowork_buf, sizeof(cowork_buf));
    if(ImGui::Button("Envoyer")) cowork_share(cowork_buf);

    ImGui::InputText("Commande openclaw", openclaw_buf, sizeof(openclaw_buf));
    if(ImGui::Button("Exécuter")) openclaw_execute(openclaw_buf);

    ImGui::InputText("URL", url_buf, sizeof(url_buf));
    if(ImGui::Button("Télécharger") && !fetch_running) fetch_internet(url_buf);
    ImGui::SameLine();
    if(fetch_running) ImGui::Text("Téléchargement en cours...");

    ImGui::Separator();
    ImGui::TextWrapped("Réponse / Log :");
    ImGui::InputTextMultiline("##response", response_text, sizeof(response_text),
                              ImVec2(-1,150), ImGuiInputTextFlags_ReadOnly);
    ImGui::EndChild();
}
EOF

cat > src/pipeline.h << 'EOF'
#pragma once
#include <vector>
#include <cstdint>

enum QuantLevel { Q4, Q6, Q8, F16, F32 };

struct Image {
    int width, height;
    std::vector<float> data;
};

Image turbo_quant(const Image& in, QuantLevel level);
std::vector<uint64_t> compute_hash(const Image& in);
std::vector<Image> multiplex(const Image& in, int streams);
std::vector<Image> process_streams(const std::vector<Image>& streams);
Image demultiplex(const std::vector<Image>& streams);
Image dehash(const std::vector<uint64_t>& hashes, const Image& ref);
Image process_image(const Image& input, QuantLevel level);
EOF

cat > src/pipeline.cpp << 'EOF'
#include "pipeline.h"
#include <cmath>
#include <algorithm>

Image turbo_quant(const Image& in, QuantLevel level) {
    Image out = in;
    float factor = (level==Q4)?16.f:(level==Q6)?8.f:(level==Q8)?4.f:(level==F16)?2.f:1.f;
    for(auto& v : out.data) v = std::round(v*factor)/factor;
    return out;
}

std::vector<uint64_t> compute_hash(const Image& in) {
    std::vector<uint64_t> hashes;
    int bw = in.width/8, bh = in.height/8;
    for(int by=0; by<8; ++by) for(int bx=0; bx<8; ++bx) {
        float sum=0; int cnt=0;
        for(int y=by*bh; y<(by+1)*bh && y<in.height; ++y)
            for(int x=bx*bw; x<(bx+1)*bw && x<in.width; ++x) {
                int idx = (y*in.width + x) * (in.data.size()/(in.width*in.height));
                sum += in.data[idx]; cnt++;
            }
        hashes.push_back((uint64_t)(sum/cnt*1000000));
    }
    return hashes;
}

std::vector<Image> multiplex(const Image& in, int streams) {
    std::vector<Image> result;
    int strip_h = in.height / streams;
    for(int s=0; s<streams; ++s) {
        Image strip; strip.width = in.width; strip.height = strip_h;
        int start_y = s*strip_h;
        for(int y=0; y<strip_h; ++y) for(int x=0; x<in.width; ++x) {
            int idx_in = ((start_y+y)*in.width + x) * (in.data.size()/(in.width*in.height));
            int idx_out = (y*in.width + x) * (in.data.size()/(in.width*in.height));
            for(size_t c=0; c<in.data.size()/(in.width*in.height); ++c)
                strip.data.push_back(in.data[idx_in+c]);
        }
        result.push_back(strip);
    }
    return result;
}

std::vector<Image> process_streams(const std::vector<Image>& streams) {
    // Factice : on renvoie les streams inchangés
    return streams;
}

Image demultiplex(const std::vector<Image>& streams) {
    if(streams.empty()) return {0,0,{}};
    int w=streams[0].width, total_h=0;
    for(auto& s: streams) total_h += s.height;
    Image out; out.width=w; out.height=total_h; out.data.resize(w*total_h*3);
    int y_off=0;
    for(auto& s: streams) {
        for(int y=0; y<s.height; ++y) for(int x=0; x<w; ++x) {
            int idx_in = (y*w+x)*3;
            int idx_out = ((y_off+y)*w+x)*3;
            for(int c=0;c<3;++c) out.data[idx_out+c] = s.data[idx_in+c];
        }
        y_off += s.height;
    }
    return out;
}

Image dehash(const std::vector<uint64_t>&, const Image& ref) { return ref; }

Image process_image(const Image& input, QuantLevel level) {
    auto quant = turbo_quant(input, level);
    auto hashes = compute_hash(quant);
    auto streams = multiplex(quant, 4);
    auto processed = process_streams(streams);
    auto demux = demultiplex(processed);
    return dehash(hashes, demux);
}
EOF

cat > src/gui.h << 'EOF'
#pragma once
namespace GUI { void render(); }
EOF

cat > src/gui.cpp << 'EOF'
#include "gui.h"
#include "display_controls.h"
#include "test_patterns.h"
#include "ai_integration.h"
#include "progress_bars.h"
#include "utils.h"
#include <imgui.h>

static int current_tab = 0;
const char* tab_names[] = { "Réglages", "Tests", "AI & Réseau", "Infos" };

void GUI::render() {
    ImGui::SetNextWindowSize(ImVec2(1200,700), ImGuiCond_FirstUseEver);
    ImGui::Begin("Phœnix-screen", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse);

    if(ImGui::BeginTabBar("MainTabs")) {
        for(int i=0;i<4;++i) {
            if(ImGui::BeginTabItem(tab_names[i])) {
                current_tab = i;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    switch(current_tab) {
        case 0: render_settings(); break;
        case 1: render_tests(); break;
        case 2: render_ai(); break;
        case 3:
            ImGui::Text("Phœnix-screen v1.0");
            ImGui::Text("Build pour Debian 13 - GTX 970");
            ImGui::Text("RAM: 32 Go | eGPU RTX 5060 (8 Go)");
            ImGui::Text("Sortie écran détectée: %s", get_output_name().c_str());
            ImGui::Text("Configuration chargée depuis phoenix_config.json");
            break;
    }

    render_progress_bars();
    ImGui::End();
}
EOF

cat > src/main.cpp << 'EOF'
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "gui.h"
#include "display_controls.h"
#include "test_patterns.h"
#include "ai_integration.h"
#include "progress_bars.h"
#include <iostream>

int main() {
    if(!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Phœnix-screen", nullptr, nullptr);
    if(!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    init_progress_bars();
    init_display_controls();
    init_test_patterns();
    init_ai_integration();

    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        GUI::render();
        ImGui::Render();
        int w,h; glfwGetFramebufferSize(window, &w, &h);
        glViewport(0,0,w,h);
        glClearColor(0.1f,0.1f,0.1f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    save_current_config();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
EOF

echo "Tous les fichiers sources ont été créés dans src/"