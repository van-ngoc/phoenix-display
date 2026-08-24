#include "test_patterns.h"
#include "utils.h"
#include <imgui.h>
#include <GL/gl.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

static GLuint test_texture = 0;
static int tex_width = 1024, tex_height = 1024;
static bool test_active = false;
static int current_test = 0;
static int solid_color_idx = 0; // Pour test uniformité
static int preview_size_idx = 1; // 0=350px, 1=500px, 2=650px

struct TestInfo {
    const char* name;
    const char* description;
};
static const TestInfo g_tests[] = {
    { "Mire Barres de Couleurs SMPTE / EBU", "Barres de couleurs broadcast étalons (Blanc, Jaune, Cyan, Vert, Magenta, Rouge, Bleu, Noir) avec signaux PLUGE pour calibrer la dynamique vidéo." },
    { "Mire Finesse & Netteté (Micro-grille 1px/2px)", "Mire haute fréquence à 1 pixel et cercles concentriques Siemens pour déceler l'accentuation artificielle de netteté et le moiré." },
    { "Mire Géométrie & Overscan (16:9 / 4:3)", "Grille d'alignement avec repères 16:9 et 4:3, lignes de cadrage et cercles de coins pour détecter les déformations et le rognage d'image." },
    { "Mire Roue Chromatique (360° HSV/HSI)", "Roue chromatique intégrale à 360° pour vérifier la continuité et la fidélité des teintes spectrales sans banding." },
    { "Mire Dégradés RVB & Échelle de Gris", "Dégradés linéaires 0-255 séparés pour Rouge, Vert, Bleu et Blanc pour tester le banding et la profondeur de couleur." },
    { "Mire PLUGE & Niveaux de Noirs (0% à 8%)", "Barres graduées ultra-sombres pour régler la luminosité et déboucher les ombres sans dégrader le niveau de noir." },
    { "Mire Hautes Lumières & Clipping (92% à 100%)", "Barres graduées très lumineuses pour calibrer le contraste sans brûler les blancs." },
    { "Mire Uniformité & Pixels Morts (Plein Écran)", "Écran unicolore uni (Rouge, Vert, Bleu, Blanc, Noir) pour identifier les pixels morts/bloqués et fuites de retro-éclairage." },
    { "Mire Damier Contrasté (Checkerboard)", "Damier noir et blanc haute fréquence pour évaluer le contraste ANSI local et les sauts d'image." },
    { "Mire Test de Ghosting / Rémanence", "Motif alterné pour mesurer le temps de réponse des pixels, le flou de mouvement et l'effet de rémanence (ghosting)." },
    { "Mire Échelons de Gris Quantifiés (16 pas)", "Échelle quantifiée de 16 niveaux de luminance pour vérifier la régularité et la linéarité du gamma." },
    { "Mire Barres HD Multi-Formats (ARIB STD-B28)", "Mire de barres de couleurs HD multi-formats avec sous-blocs de référence 75%/100% et signaux PLUGE fins." },
    { "Mire Échelle Gamma Visuelle (1.8 à 2.6)", "Bandes comparatives trame fixe / solide pour vérifier le gamma natif du moniteur de 1.8 à 2.6 sans sonde." },
    { "Mire Fresnel Zone Plate (Aliasing & Filter)", "Plaque de zone de Fresnel circulaire à fréquence spatiale quadratique pour détecter l'aliasing, le moiré et les artefacts d'anti-aliasing." },
    { "Mire Convergence Tri-Couleur R/G/B", "Grille croisée micro-fine à canaux séparés Rouge, Vert, Bleu pour vérifier l'alignement mécanique/électronique des sous-pixels." },
    { "Mire Matrice 2D HSL (Teinte x Saturation)", "Plan bidimensionnel cartographiant la teinte en X et la saturation en Y pour évaluer la couverture du gamut." },
    { "Mire MacBeth ColorChecker (24 Patchs)", "Les 24 teintes étalons MacBeth ColorChecker (Tons chair, ciel, végétation, primaires et neutres) pour la fidélité des couleurs." },
    { "Mire Dynamic Range Starburst (Contrast HDR)", "Étoile radiale à 32 échelons de luminance et pointes de contraste pour évaluer le blooming et le contraste local." }
};

static constexpr int NUM_TESTS = sizeof(g_tests) / sizeof(g_tests[0]);

static void generate_test_pattern(int id, std::vector<unsigned char>& pixels) {
    for (int y = 0; y < tex_height; ++y) {
        for (int x = 0; x < tex_width; ++x) {
            int idx = (y * tex_width + x) * 3;
            float fx = (float)x / (tex_width - 1);
            float fy = (float)y / (tex_height - 1);
            unsigned char r = 0, g = 0, b = 0;

            switch (id) {
                case 0: { // SMPTE / EBU Color Bars
                    if (fy < 0.67f) {
                        int bar = (int)(fx * 7.0f);
                        switch (bar) {
                            case 0: r=255; g=255; b=255; break; // White
                            case 1: r=255; g=255; b=0;   break; // Yellow
                            case 2: r=0;   g=255; b=255; break; // Cyan
                            case 3: r=0;   g=255; b=0;   break; // Green
                            case 4: r=255; g=0;   b=255; break; // Magenta
                            case 5: r=255; g=0;   b=0;   break; // Red
                            case 6: default: r=0; g=0; b=255; break; // Blue
                        }
                    } else if (fy < 0.75f) {
                        int bar = (int)(fx * 7.0f);
                        switch (bar) {
                            case 0: r=0;   g=0;   b=255; break;
                            case 1: r=19;  g=19;  b=19;  break;
                            case 2: r=255; g=0;   b=255; break;
                            case 3: r=19;  g=19;  b=19;  break;
                            case 4: r=0;   g=255; b=255; break;
                            case 5: r=19;  g=19;  b=19;  break;
                            case 6: default: r=255; g=255; b=255; break;
                        }
                    } else {
                        if (fx < 0.18f)      { r=0;   g=33;  b=76;  } // +I
                        else if (fx < 0.36f) { r=255; g=255; b=255; } // White
                        else if (fx < 0.54f) { r=50;  g=0;   b=106; } // +Q
                        else if (fx < 0.72f) { r=19;  g=19;  b=19;  } // Black
                        else if (fx < 0.81f) { r=0;   g=0;   b=0;   } // Sub-black
                        else if (fx < 0.90f) { r=35;  g=35;  b=35;  } // Super-black
                        else                 { r=19;  g=19;  b=19;  }
                    }
                    break;
                }
                case 1: { // Finesse & Netteté
                    if (fy < 0.5f) {
                        float cx = fx - 0.5f;
                        float cy = fy - 0.25f;
                        float dist = sqrtf(cx * cx + cy * cy);
                        float angle = atan2f(cy, cx);
                        if (dist < 0.22f) {
                            bool ray = (sinf(angle * 36.0f) > 0.0f);
                            bool ring = ((int)(dist * 200.0f) % 2 == 0);
                            r = g = b = (ray ^ ring) ? 255 : 0;
                        } else {
                            r = g = b = 40;
                        }
                    } else {
                        if (fx < 0.33f) {
                            r = g = b = (x % 2 == 0) ? 255 : 0;
                        } else if (fx < 0.66f) {
                            r = g = b = ((x / 2) % 2 == 0) ? 255 : 0;
                        } else {
                            r = g = b = (y % 2 == 0) ? 255 : 0;
                        }
                    }
                    break;
                }
                case 2: { // Géométrie & Overscan
                    r = g = b = 25;
                    // Outer border
                    if (x < 4 || x >= tex_width - 4 || y < 4 || y >= tex_height - 4) {
                        r = g = b = 255;
                    }
                    // Grid lines every 10%
                    else if ((x % (tex_width / 10)) == 0 || (y % (tex_height / 10)) == 0) {
                        r = g = b = 100;
                    }
                    // Center crosshair
                    if (std::abs(x - tex_width / 2) <= 1) { r = 255; g = 50; b = 50; }
                    if (std::abs(y - tex_height / 2) <= 1) { r = 50; g = 255; b = 50; }

                    // Corner circles
                    float float_corners[4][2] = {{0.15f,0.15f}, {0.85f,0.15f}, {0.15f,0.85f}, {0.85f,0.85f}};
                    for (int c=0; c<4; ++c) {
                        float dx = fx - float_corners[c][0];
                        float dy = fy - float_corners[c][1];
                        float d = sqrtf(dx*dx + dy*dy);
                        if (fabsf(d - 0.08f) < 0.003f) { r = g = b = 255; }
                    }
                    break;
                }
                case 3: { // Roue Chromatique 360°
                    float dx = fx - 0.5f;
                    float dy = fy - 0.5f;
                    float dist = sqrtf(dx*dx + dy*dy) * 2.0f;
                    if (dist > 1.0f) {
                        r = g = b = 20;
                    } else {
                        float angle = atan2f(dy, dx) * (180.0f / 3.14159265f);
                        if (angle < 0.0f) angle += 360.0f;
                        float rf, gf, bf;
                        hsv_to_rgb(angle, dist, 1.0f, rf, gf, bf);
                        r = (unsigned char)(rf * 255.0f);
                        g = (unsigned char)(gf * 255.0f);
                        b = (unsigned char)(bf * 255.0f);
                    }
                    break;
                }
                case 4: { // Dégradés RVB & Échelle de gris
                    float val = fx * 255.0f;
                    unsigned char v = (unsigned char)val;
                    if (fy < 0.25f)      { r = v; g = 0; b = 0; }
                    else if (fy < 0.50f) { r = 0; g = v; b = 0; }
                    else if (fy < 0.75f) { r = 0; g = 0; b = v; }
                    else                 { r = g = b = v; }
                    break;
                }
                case 5: { // PLUGE & Niveaux de Noirs (0% à 8%)
                    int step = (int)(fx * 6.0f);
                    float levels[] = {0.0f, 0.015f, 0.03f, 0.045f, 0.06f, 0.08f};
                    float val = levels[std::clamp(step, 0, 5)] * 255.0f;
                    r = g = b = (unsigned char)val;
                    // Grid separator
                    if (x % (tex_width / 6) == 0) { r = g = b = 40; }
                    break;
                }
                case 6: { // Hautes Lumières & Clipping (92% à 100%)
                    int step = (int)(fx * 6.0f);
                    float levels[] = {0.92f, 0.94f, 0.96f, 0.98f, 0.99f, 1.0f};
                    float val = levels[std::clamp(step, 0, 5)] * 255.0f;
                    r = g = b = (unsigned char)val;
                    if (x % (tex_width / 6) == 0) { r = g = b = 180; }
                    break;
                }
                case 7: { // Uniformité & Pixels Morts (Plein Écran)
                    switch (solid_color_idx) {
                        case 0: r=255; g=0;   b=0;   break; // Red
                        case 1: r=0;   g=255; b=0;   break; // Green
                        case 2: r=0;   g=0;   b=255; break; // Blue
                        case 3: r=255; g=255; b=255; break; // White
                        case 4: default: r=0; g=0; b=0; break; // Black
                    }
                    break;
                }
                case 8: { // Damier Contrasté & Saut d'image
                    bool ch = ((int)(fx * 16.0f) % 2 == 0) ^ ((int)(fy * 16.0f) % 2 == 0);
                    r = g = b = ch ? 255 : 0;
                    break;
                }
                case 9: { // Ghosting / Rémanence
                    bool grid = ((int)(fx * 24.0f) % 2 == 0) ^ ((int)(fy * 24.0f) % 2 == 0);
                    r = grid ? 255 : 0;
                    g = grid ? 0 : 255;
                    b = 0;
                    break;
                }
                case 10: { // Échelons de Gris (16 pas)
                    int steps = 16;
                    int step = (int)(fx * steps);
                    int val = step * (255 / (steps - 1));
                    r = g = b = (unsigned char)std::clamp(val, 0, 255);
                    break;
                }
                case 11: { // Barres HD Multi-Formats (ARIB STD-B28)
                    if (fy < 0.60f) {
                        // 75% Color bars
                        int bar = (int)(fx * 7.0f);
                        unsigned char colors[][3] = {
                            {191,191,191}, {191,191,0}, {0,191,191}, {0,191,0},
                            {191,0,191}, {191,0,0}, {0,0,191}
                        };
                        int b_idx = std::clamp(bar, 0, 6);
                        r = colors[b_idx][0]; g = colors[b_idx][1]; b = colors[b_idx][2];
                    } else if (fy < 0.75f) {
                        // 100% Color bars
                        int bar = (int)(fx * 7.0f);
                        unsigned char colors[][3] = {
                            {255,255,255}, {255,255,0}, {0,255,255}, {0,255,0},
                            {255,0,255}, {255,0,0}, {0,0,255}
                        };
                        int b_idx = std::clamp(bar, 0, 6);
                        r = colors[b_idx][0]; g = colors[b_idx][1]; b = colors[b_idx][2];
                    } else {
                        // Multi-step PLUGE ramp
                        if (fx < 0.2f) { r = 15; g = 15; b = 15; }
                        else if (fx < 0.4f) { r = 0; g = 0; b = 0; }
                        else if (fx < 0.6f) { r = 30; g = 30; b = 30; }
                        else if (fx < 0.8f) { r = 60; g = 60; b = 60; }
                        else { r = 120; g = 120; b = 120; }
                    }
                    break;
                }
                case 12: { // Échelle Gamma Visuelle (1.8 à 2.6)
                    int band = (int)(fy * 5.0f);
                    float target_gammas[] = { 1.8f, 2.0f, 2.2f, 2.4f, 2.6f };
                    float g_val = target_gammas[std::clamp(band, 0, 4)];
                    float input_val = fx;
                    float linear_val = std::pow(input_val, g_val);
                    // Alternance entre dither à 50% et aplat continu
                    bool pattern = ((x + y) % 2 == 0);
                    if (pattern) {
                        float v = linear_val * 255.0f;
                        r = g = b = (unsigned char)std::clamp((int)v, 0, 255);
                    } else {
                        float v = input_val * 255.0f;
                        r = g = b = (unsigned char)std::clamp((int)v, 0, 255);
                    }
                    break;
                }
                case 13: { // Fresnel Zone Plate (Aliasing 2D)
                    float cx = (fx - 0.5f) * 2.0f;
                    float cy = (fy - 0.5f) * 2.0f;
                    float radius_sq = cx * cx + cy * cy;
                    float k = 120.0f;
                    float val = 0.5f + 0.5f * std::cos(k * radius_sq);
                    unsigned char c = (unsigned char)(val * 255.0f);
                    r = g = b = c;
                    break;
                }
                case 14: { // Convergence Tri-Couleur R/G/B
                    r = g = b = 15;
                    int grid_size = 32;
                    if ((x % grid_size) == 0) r = 255;
                    if ((y % grid_size) == 0) g = 255;
                    if (((x + y) % grid_size) == 0) b = 255;
                    break;
                }
                case 15: { // Matrice 2D HSL (Teinte x Saturation)
                    float hue = fx * 360.0f;
                    float sat = 1.0f - fy;
                    float rf, gf, bf;
                    hsv_to_rgb(hue, sat, 1.0f, rf, gf, bf);
                    r = (unsigned char)(rf * 255.0f);
                    g = (unsigned char)(gf * 255.0f);
                    b = (unsigned char)(bf * 255.0f);
                    break;
                }
                case 16: { // MacBeth ColorChecker (24 Patchs Référence)
                    int col = (int)(fx * 6.0f);
                    int row = (int)(fy * 4.0f);
                    int patch_idx = std::clamp(row * 6 + col, 0, 23);
                    static const unsigned char macbeth[24][3] = {
                        {115, 82, 68},  {194, 150, 130}, {98, 122, 157}, {87, 108, 67},  {133, 128, 177}, {103, 189, 170},
                        {214, 126, 44}, {80, 91, 166},   {193, 90, 99},  {94, 60, 108},  {157, 188, 64},  {224, 163, 46},
                        {56, 61, 150},  {70, 148, 73},   {175, 54, 60},  {231, 199, 31}, {187, 86, 149},  {8, 133, 161},
                        {243, 243, 242},{200, 200, 200}, {160, 160, 160},{122, 122, 121},{85, 85, 85},    {52, 52, 52}
                    };
                    // Inner margin border around patches
                    float local_x = fmodf(fx * 6.0f, 1.0f);
                    float local_y = fmodf(fy * 4.0f, 1.0f);
                    if (local_x < 0.05f || local_x > 0.95f || local_y < 0.05f || local_y > 0.95f) {
                        r = g = b = 20;
                    } else {
                        r = macbeth[patch_idx][0];
                        g = macbeth[patch_idx][1];
                        b = macbeth[patch_idx][2];
                    }
                    break;
                }
                case 17: default: { // Dynamic Range Starburst (Contrast HDR)
                    float cx = fx - 0.5f;
                    float cy = fy - 0.5f;
                    float dist = sqrtf(cx * cx + cy * cy) * 2.0f;
                    float angle = atan2f(cy, cx);
                    if (angle < 0.0f) angle += 2.0f * 3.14159265f;

                    if (dist > 0.95f) {
                        r = g = b = 10;
                    } else if (dist < 0.15f) {
                        r = g = b = 255;
                    } else {
                        int wedge = (int)(angle / (2.0f * 3.14159265f / 32.0f));
                        float level = (float)wedge / 31.0f;
                        unsigned char val = (unsigned char)(level * 255.0f);
                        r = g = b = val;
                    }
                    break;
                }
            }
            pixels[idx]     = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
        }
    }
}

void init_test_patterns() {
    glGenTextures(1, &test_texture);
    glBindTexture(GL_TEXTURE_2D, test_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tex_width, tex_height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    std::vector<unsigned char> pixels(tex_width * tex_height * 3, 128);
    generate_test_pattern(0, pixels);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_width, tex_height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
}

static bool g_immersive_fullscreen = false;

void draw_test(int test_id) {
    if (!test_active) return;
    std::vector<unsigned char> pixels(tex_width * tex_height * 3);
    generate_test_pattern(test_id, pixels);
    glBindTexture(GL_TEXTURE_2D, test_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_width, tex_height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    ImVec2 img_size;
    if (preview_size_idx == 0) { // Adapté à la fenêtre
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float side = std::max(200.0f, std::min(avail.x, avail.y - 10.0f));
        img_size = ImVec2(side, side);
    } else {
        float sizes[] = { 350.0f, 500.0f, 700.0f, 1000.0f };
        float s = sizes[std::clamp(preview_size_idx - 1, 0, 3)];
        img_size = ImVec2(s, s);
    }

    ImGui::Image((void*)(intptr_t)test_texture, img_size);
}

void render_tests() {
    ImGui::BeginChild("Tests", ImVec2(0, -50), true);

    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "--- Mires de Tests & Étalonnage Écran ---");
    ImGui::Separator();

    static const char* test_names_combo[NUM_TESTS];
    for (int i = 0; i < NUM_TESTS; ++i) test_names_combo[i] = g_tests[i].name;

    ImGui::Combo("Sélectionner une mire", &current_test, test_names_combo, NUM_TESTS);

    if (current_test == 7) { // Uniformité & Pixels Morts
        const char* solid_colors[] = { "Rouge Pur", "Vert Pur", "Bleu Pur", "Blanc Pur", "Noir Pur" };
        ImGui::Combo("Couleur de test", &solid_color_idx, solid_colors, IM_ARRAYSIZE(solid_colors));
    }

    const char* sizes[] = { "Adapté à la fenêtre (Auto)", "Petite (350px)", "Moyenne (500px)", "Grande (700px)", "Très Grande (1000px)" };
    ImGui::Combo("Taille d'affichage", &preview_size_idx, sizes, IM_ARRAYSIZE(sizes));

    if (ImGui::Button("Activer la mire")) test_active = true;
    ImGui::SameLine();
    if (ImGui::Button("Désactiver")) test_active = false;
    ImGui::SameLine();
    if (ImGui::Button("Mire Plein Écran Absolu (Échap pour fermer)")) {
        test_active = true;
        g_immersive_fullscreen = true;
    }

    ImGui::Separator();

    if (current_test >= 0 && current_test < NUM_TESTS) {
        ImGui::TextWrapped("Utilisation : %s", g_tests[current_test].description);
    }

    ImGui::Spacing();

    if (test_active) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "Mire active : %s", g_tests[current_test].name);
        draw_test(current_test);
    } else {
        ImGui::TextDisabled("Aucune mire actuellement affichée. Cliquez sur 'Activer la mire'.");
    }

    ImGui::EndChild();

    // Mode Plein Écran Immersif
    if (g_immersive_fullscreen && test_active) {
        std::vector<unsigned char> pixels(tex_width * tex_height * 3);
        generate_test_pattern(current_test, pixels);
        glBindTexture(GL_TEXTURE_2D, test_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_width, tex_height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("ImmersivePatternOverlay", nullptr, 
            ImGuiWindowFlags_NoDecoration | 
            ImGuiWindowFlags_NoMove | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoSavedSettings);

        ImGui::Image((void*)(intptr_t)test_texture, vp->Size);

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(0)) {
            g_immersive_fullscreen = false;
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
}