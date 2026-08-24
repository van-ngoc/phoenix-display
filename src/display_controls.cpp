#include "display_controls.h"
#include "utils.h"
#include "config.h"
#include <imgui.h>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#ifdef HAVE_GPS
#include <gps.h>
#endif

DisplayConfig g_config;
static std::string g_output_name;
static float lut_curve[256];
static bool lut_dirty = false;

// Résolutions connues de l'app, partagées entre le combo "Résolution" et
// apply_aspect_ratio() (pour retrouver une résolution qui respecte un ratio).
static const char* g_known_resolutions[] = {
    "3840x2160", "2560x1440", "1920x1080", "1728x1000",
    "1600x900", "1440x1080", "1280x720", "1024x768", "800x600"
};

// Message affiché sous le panneau de réglages, pour que l'utilisateur voie
// si un bouton "Appliquer" a réellement fonctionné ou pourquoi il a échoué
// (auparavant les échecs xrandr étaient silencieux).
static std::string g_status_message;
static bool g_status_ok = true;

static void set_status(bool ok, const std::string& msg) {
    g_status_ok = ok;
    g_status_message = msg;
}

static bool run_cmd(const std::string& cmd) {
    return system(cmd.c_str()) == 0;
}

void init_display_controls() {
    g_output_name = get_output_name();
    g_config = load_config();
    for (int i=0;i<256;++i) lut_curve[i] = g_config.lut[i];
    
    // Applique uniquement les réglages sûrs (gamma, luminosité, contraste)
    apply_gamma(g_config.gamma);
    apply_brightness(g_config.brightness);
    apply_contrast(g_config.contrast);
    
    // Les réglages de résolution, ratio, orientation et mode couleur
    // ne sont pas appliqués automatiquement pour éviter les erreurs.
    // L'utilisateur les activera via l'interface.
}

// Gain linéaire (luminance + couleurs) par canal : appliqué APRÈS la mise en
// forme gamma/contraste, comme un simple facteur multiplicatif sur la valeur
// encodée. Ne doit surtout pas être mélangé à gamma/contraste dans un même
// exposant (voir compute_gamma_exponent / compute_contrast) : c'est ce qui
// causait un exposant pouvant grimper jusqu'à 10 et un écran "trop contrasté"
// (noirs bouchés) dès que la luminance ou le blanc descendait sous 1.0.
static void compute_effective_gains(float& r_gain, float& g_gain, float& b_gain) {
    float brightness_gain = std::clamp(g_config.brightness / 300.0f, 0.2f, 2.0f);
    float white_mult = (g_config.white <= 0.01f) ? 1.0f : std::clamp(g_config.white, 0.1f, 2.0f);
    float base_gain = brightness_gain * white_mult;
    r_gain = std::clamp(g_config.rgb[0] * (1.0f - g_config.cmyk[0]) * (1.0f - g_config.cmyk[1]) * (1.0f - g_config.cmyk[3]) * base_gain, 0.1f, 2.0f);
    g_gain = std::clamp(g_config.rgb[1] * (1.0f - g_config.cmyk[0]) * (1.0f - g_config.cmyk[2]) * (1.0f - g_config.cmyk[3]) * base_gain, 0.1f, 2.0f);
    b_gain = std::clamp(g_config.rgb[2] * (1.0f - g_config.cmyk[1]) * (1.0f - g_config.cmyk[2]) * (1.0f - g_config.cmyk[3]) * base_gain, 0.1f, 2.0f);
}

// Exposant gamma pur, relatif à 2.2 (valeur "neutre" du combo) : gamma=2.2
// ne modifie donc pas la courbe, gamma=1.6..2.6 reste dans [0.85, 1.38].
static float compute_gamma_exponent() {
    return 2.2f / std::clamp(g_config.gamma, 1.0f, 3.0f);
}

// Contraste appliqué comme un vrai étirement autour du point médian (0.5),
// plutôt que mélangé à l'exposant gamma : contraste=1 est neutre.
static float apply_contrast_pivot(float v, float contrast) {
    return std::clamp(0.5f + (v - 0.5f) * contrast, 0.0f, 1.0f);
}

// xrandr (CLI) et la LUT/ombres-hautes lumières écrivaient tous les deux dans
// l'unique table de gamma matérielle de la sortie, chacun écrasant l'autre.
// Tout (luminance, couleurs, gamma, contraste, ombres/hautes lumières) passe
// maintenant par cette unique fonction Xlib/XRandR, qui combine la courbe LUT
// de base (lut_curve), l'exposant gamma, l'étirement de contraste et le gain
// linéaire couleur/luminance (r_gain/g_gain/b_gain) en un seul XRRSetCrtcGamma.
static bool push_gamma_ramp(float r_gain, float g_gain, float b_gain) {
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        set_status(false, "Impossible d'ouvrir la connexion X11");
        return false;
    }

    XRRScreenResources* res = XRRGetScreenResources(dpy, DefaultRootWindow(dpy));
    if (!res) {
        XCloseDisplay(dpy);
        set_status(false, "xrandr: ressources d'écran introuvables");
        return false;
    }

    RRCrtc target_crtc = 0;
    for (int i = 0; i < res->noutput && !target_crtc; ++i) {
        XRROutputInfo* out_info = XRRGetOutputInfo(dpy, res, res->outputs[i]);
        if (out_info) {
            if (out_info->crtc != 0 && g_output_name == std::string(out_info->name, out_info->nameLen)) {
                target_crtc = out_info->crtc;
            }
            XRRFreeOutputInfo(out_info);
        }
    }
    if (!target_crtc) {
        XRRFreeScreenResources(res);
        XCloseDisplay(dpy);
        set_status(false, "Sortie '" + g_output_name + "' introuvable ou inactive");
        return false;
    }

    int gamma_size = XRRGetCrtcGammaSize(dpy, target_crtc);
    if (gamma_size <= 0) {
        XRRFreeScreenResources(res);
        XCloseDisplay(dpy);
        set_status(false, "Cette sortie ne supporte pas de table de gamma");
        return false;
    }

    float gamma_exp = compute_gamma_exponent();

    XRRCrtcGamma* gamma = XRRAllocGamma(gamma_size);
    for (int i = 0; i < gamma_size; ++i) {
        float pos = (gamma_size > 1) ? (float)i / (gamma_size - 1) * 255.0f : 0.0f;
        int idx0 = (int)pos;
        int idx1 = std::min(idx0 + 1, 255);
        float frac = pos - idx0;
        float base = std::clamp(lut_curve[idx0] * (1.0f - frac) + lut_curve[idx1] * frac, 0.0f, 1.0f);

        float shaped = apply_contrast_pivot(powf(base, gamma_exp), g_config.contrast);

        float r = std::clamp(shaped * r_gain, 0.0f, 1.0f);
        float g = std::clamp(shaped * g_gain, 0.0f, 1.0f);
        float b = std::clamp(shaped * b_gain, 0.0f, 1.0f);

        gamma->red[i]   = (unsigned short)(r * 65535.0f);
        gamma->green[i] = (unsigned short)(g * 65535.0f);
        gamma->blue[i]  = (unsigned short)(b * 65535.0f);
    }

    XRRSetCrtcGamma(dpy, target_crtc, gamma);
    XSync(dpy, False);
    XRRFreeGamma(gamma);
    XRRFreeScreenResources(res);
    XCloseDisplay(dpy);
    return true;
}

static bool apply_combined_gamma() {
    float r_gain, g_gain, b_gain;
    compute_effective_gains(r_gain, g_gain, b_gain);
    bool ok = push_gamma_ramp(r_gain, g_gain, b_gain);
    if (ok) set_status(true, "Réglages d'image appliqués");
    return ok;
}

void apply_gamma(float gamma) {
    g_config.gamma = gamma;
    apply_combined_gamma();
}

void apply_brightness(int lux) {
    // Passe par la table de gamma combinée (push_gamma_ramp) au lieu de
    // "xrandr --brightness", qui réinitialisait toute la table de gamma
    // matérielle avec une rampe linéaire et écrasait gamma/contraste/couleurs
    // appliqués par les autres boutons (et vice versa).
    g_config.brightness = lux;
    if (apply_combined_gamma()) set_status(true, "Luminance appliquée");
    else set_status(false, "Échec de l'application de la luminance");
}

void apply_contrast(float contrast) {
    g_config.contrast = contrast;
    apply_combined_gamma();
}

void apply_resolution(int w, int h) {
    // Vérifie si le mode existe déjà
    std::string check = exec_cmd(("xrandr | grep -F '" + std::to_string(w) + "x" + std::to_string(h) + "'").c_str());
    if (check.empty()) {
        // Le mode n'existe pas, on pourrait le créer mais c'est complexe.
        set_status(false, "Mode " + std::to_string(w) + "x" + std::to_string(h) + " non supporté par cet écran");
        return;
    }
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "xrandr --output %s --mode %dx%d", g_output_name.c_str(), w, h);
    if (run_cmd(cmd)) set_status(true, "Résolution appliquée: " + std::to_string(w) + "x" + std::to_string(h));
    else set_status(false, "Échec de l'application de la résolution");
}

void apply_refresh(int hz) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "xrandr --output %s --rate %d", g_output_name.c_str(), hz);
    if (run_cmd(cmd)) set_status(true, "Rafraîchissement appliqué: " + std::to_string(hz) + " Hz");
    else set_status(false, std::to_string(hz) + " Hz non supporté pour la résolution actuelle");
}

void apply_aspect_ratio(float w, float h) {
    if (h <= 0.0f) return;
    float target = w / h;
    int best_w = 0, best_h = 0;
    float best_diff = 0.05f; // tolérance ~5% pour absorber les ratios non ronds (ex: 1728x1000)
    for (const char* res : g_known_resolutions) {
        int rw, rh;
        if (sscanf(res, "%dx%d", &rw, &rh) != 2 || rh <= 0) continue;
        float diff = fabsf((float)rw / rh - target);
        if (diff < best_diff) { best_diff = diff; best_w = rw; best_h = rh; }
    }
    if (best_w == 0) {
        set_status(false, "Aucune résolution connue ne correspond à ce ratio");
        return;
    }
    g_config.res_w = best_w;
    g_config.res_h = best_h;
    apply_resolution(best_w, best_h);
}

void apply_orientation(const std::string& mode) {
    std::string cmd = "xrandr --output " + g_output_name + " --rotate ";
    if (mode == "portrait") cmd += "left";
    else if (mode == "paysage") cmd += "normal";
    else return;
    if (run_cmd(cmd)) set_status(true, "Orientation appliquée");
    else set_status(false, "Échec de l'application de l'orientation");
}

void apply_lut(const float* curve) {
    for (int i=0;i<256;++i) {
        g_config.lut[i] = curve[i];
        lut_curve[i] = curve[i];
    }
    lut_dirty = false;
    if (apply_combined_gamma()) set_status(true, "Courbe LUT appliquée");
}

void apply_color_adjust(float r, float g, float b, float c, float m, float y, float w, float k) {
    g_config.rgb = {r, g, b};
    g_config.cmyk = {c, m, y, k};
    g_config.white = w;
    apply_combined_gamma();
}

void apply_color_mode(int mode) {
    if (mode == 0) { // sRGB
        g_config.gamma = 2.2f;
        apply_gamma(2.2f);
        set_status(true, "Mode couleur configuré sur sRGB (Standard)");
    } else if (mode == 1) { // DCI-P3
        g_config.gamma = 2.6f;
        apply_gamma(2.6f);
        set_status(true, "Mode couleur configuré sur DCI-P3 (Cinéma)");
    } else if (mode == 2) { // Adobe RGB (1998)
        g_config.gamma = 2.1992f;
        apply_gamma(2.1992f);
        set_status(true, "Mode couleur configuré sur Adobe RGB (1998) Wide Gamut");
    } else if (mode == 3) { // Apple RGB
        g_config.gamma = 1.8f;
        apply_gamma(1.8f);
        set_status(true, "Mode couleur configuré sur Apple RGB (Mac Classic Gamma 1.8)");
    } else if (mode == 4) { // Bruce RGB
        g_config.gamma = 2.2f;
        apply_gamma(2.2f);
        set_status(true, "Mode couleur configuré sur Bruce RGB (Gamma 2.2 - D65)");
    } else if (mode == 5) { // ProPhoto RGB
        g_config.gamma = 1.8f;
        apply_gamma(1.8f);
        set_status(true, "Mode couleur configuré sur ProPhoto RGB / ROMM (Gamma 1.8 - D50)");
    } else if (mode == 6) { // ColorMatch RGB
        g_config.gamma = 1.8f;
        apply_gamma(1.8f);
        set_status(true, "Mode couleur configuré sur ColorMatch RGB (Gamma 1.8 - D50)");
    } else if (mode == 7) { // NTSC RGB
        g_config.gamma = 2.2f;
        apply_gamma(2.2f);
        set_status(true, "Mode couleur configuré sur NTSC RGB (Gamma 2.2 - White C)");
    } else if (mode == 8) { // PAL/SECAM RGB
        g_config.gamma = 2.2f;
        apply_gamma(2.2f);
        set_status(true, "Mode couleur configuré sur PAL/SECAM RGB (Gamma 2.2 - D65)");
    } else if (mode == 9) { // Wide Gamut RGB
        g_config.gamma = 2.2f;
        apply_gamma(2.2f);
        set_status(true, "Mode couleur configuré sur Wide Gamut RGB (Gamma 2.2 - D50)");
    } else {
        set_status(false, "Mode couleur non reconnu");
    }
}

static void ensure_dir(const std::string& dir) {
    mkdir(dir.c_str(), 0755); // ignore si déjà existant
}

void apply_autostart(bool enabled) {
    const char* home = getenv("HOME");
    if (!home) {
        set_status(false, "Impossible de déterminer le répertoire personnel (HOME)");
        return;
    }
    std::string autostart_dir = std::string(home) + "/.config/autostart";
    std::string desktop_file = autostart_dir + "/phoenix-display.desktop";

    if (!enabled) {
        if (remove(desktop_file.c_str()) == 0 || errno == ENOENT) {
            set_status(true, "Démarrage automatique désactivé");
        } else {
            set_status(false, "Échec de la désactivation du démarrage automatique");
        }
        return;
    }

    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) {
        set_status(false, "Impossible de déterminer le chemin de l'exécutable");
        return;
    }
    exe_path[len] = '\0';

    ensure_dir(std::string(home) + "/.config");
    ensure_dir(autostart_dir);

    std::ofstream out(desktop_file);
    if (!out.is_open()) {
        set_status(false, "Impossible d'écrire le fichier de démarrage automatique");
        return;
    }
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=Phœnix-display\n"
        << "GenericName=Contrôleur d'Affichage & Étalonnage Écran\n"
        << "Comment=Calibration d'affichage avancée, gestion des couleurs ACES, courbe de tonalité Lightroom, adaptation chromatique CAT, mires de tests vidéo et profilage écran.\n"
        << "Exec=" << exe_path << "\n"
        << "Icon=phoenix-display\n"
        << "Terminal=false\n"
        << "Categories=Settings;HardwareSettings;Graphics;Photography;Video;\n"
        << "StartupNotify=true\n"
        << "Keywords=display;screen;gamma;brightness;color;calibration;rvb;hsl;hsv;hsi;aces;lightroom;bruce-rgb;adobe-rgb;apple-rgb;cat;bradford;mires;test-patterns;color-management;\n"
        << "X-GNOME-Autostart-enabled=true\n";
    out.close();
    set_status(true, "Démarrage automatique activé");
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

static bool auto_apply_lut = true;

enum ToneCurveMode {
    CURVE_MODE_PARAMETRIC = 0,
    CURVE_MODE_POINT = 1
};
static int g_curve_mode = CURVE_MODE_PARAMETRIC;
static int g_selected_channel = 0; // 0 = Combiné (RGB), 1 = Rouge, 2 = Vert, 3 = Bleu

// --- Régleurs Paramétriques (Style Lightroom Classic) ---
static float g_param_highlights = 0.0f; // [-100.0, +100.0]
static float g_param_lights     = 0.0f; // [-100.0, +100.0]
static float g_param_darks      = 0.0f; // [-100.0, +100.0]
static float g_param_shadows    = 0.0f; // [-100.0, +100.0]

// Régleur "Affiner la saturation" (Mode Point - Lightroom Image 2)
static float g_refine_saturation = 27.0f; // [0.0, 100.0]

// Préréglage de courbe à points (0 = Personnalisé(e), 1 = Linéaire, 2 = Contraste moyen, 3 = Contraste fort)
static int g_point_curve_preset = 0;

// Séparateurs de régions tonales [0.0, 1.0]
static float g_split_shadows_darks     = 0.25f; // Separator 1
static float g_split_darks_lights      = 0.50f; // Separator 2
static float g_split_lights_highlights = 0.75f; // Separator 3

// Glissement actif de la barre de séparateur sous le graphique
static int g_dragged_split = -1;

// Mode survolé pour mise en surbrillance de région (0 = Aucun, 1 = Ombres, 2 = Tons sombres, 3 = Tons clairs, 4 = Hautes lumières)
static int g_hovered_tone_region = 0;

// --- Points de contrôle (Mode Point) ---
static std::vector<ToneCurvePoint> g_curve_points = {
    {0.00f, 0.00f},
    {0.67f, 0.37f}, // Matching exact coordinates 67/37 from image 2
    {1.00f, 1.00f}
};
static int g_selected_point = 1;
static int g_dragged_point = -1;

// Interpolation Spline Cubique Monotone (Fritsch-Carlson)
static float eval_monotone_cubic_spline(const std::vector<ToneCurvePoint>& pts, float x) {
    int n = (int)pts.size();
    if (n == 0) return x;
    if (n == 1) return pts[0].y;
    if (x <= pts[0].x) return pts[0].y;
    if (x >= pts[n - 1].x) return pts[n - 1].y;

    std::vector<float> dx(n - 1), dy(n - 1), ms(n - 1);
    for (int i = 0; i < n - 1; ++i) {
        dx[i] = pts[i + 1].x - pts[i].x;
        dy[i] = pts[i + 1].y - pts[i].y;
        ms[i] = (dx[i] > 1e-6f) ? (dy[i] / dx[i]) : 0.0f;
    }

    std::vector<float> c1s(n);
    c1s[0] = ms[0];
    c1s[n - 1] = ms[n - 2];
    for (int i = 1; i < n - 1; ++i) {
        if (ms[i - 1] * ms[i] <= 0.0f) {
            c1s[i] = 0.0f;
        } else {
            c1s[i] = (ms[i - 1] + ms[i]) * 0.5f;
        }
    }

    for (int i = 0; i < n - 1; ++i) {
        if (std::abs(ms[i]) < 1e-6f) {
            c1s[i] = 0.0f;
            c1s[i + 1] = 0.0f;
        } else {
            float alpha = c1s[i] / ms[i];
            float beta = c1s[i + 1] / ms[i];
            float distsq = alpha * alpha + beta * beta;
            if (distsq > 9.0f) {
                float tau = 3.0f / std::sqrt(distsq);
                c1s[i] = tau * alpha * ms[i];
                c1s[i + 1] = tau * beta * ms[i];
            }
        }
    }

    int idx = 0;
    for (int i = 0; i < n - 1; ++i) {
        if (x >= pts[i].x && x <= pts[i + 1].x) {
            idx = i;
            break;
        }
    }

    float h = dx[idx];
    if (h < 1e-6f) return pts[idx].y;

    float t = (x - pts[idx].x) / h;
    float t2 = t * t;
    float t3 = t2 * t;

    float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 = t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 = t3 - t2;

    float y = pts[idx].y * h00 + h * c1s[idx] * h10 + pts[idx + 1].y * h01 + h * c1s[idx + 1] * h11;
    return std::clamp(y, 0.0f, 1.0f);
}

// Calcul de la courbe paramétrique selon Lightroom (4 régleurs + 3 séparateurs)
static float eval_parametric_curve(float x) {
    float s1 = std::clamp(g_split_shadows_darks, 0.05f, 0.40f);
    float s2 = std::clamp(g_split_darks_lights, s1 + 0.05f, 0.70f);
    float s3 = std::clamp(g_split_lights_highlights, s2 + 0.05f, 0.95f);

    float sh = g_param_shadows / 100.0f;
    float dk = g_param_darks / 100.0f;
    float lt = g_param_lights / 100.0f;
    float hl = g_param_highlights / 100.0f;

    std::vector<ToneCurvePoint> param_pts = {
        { 0.0f, std::clamp(0.0f + sh * 0.15f, 0.0f, 0.30f) },
        { s1,   std::clamp(s1 + sh * 0.20f + dk * 0.10f, 0.0f, 1.0f) },
        { s2,   std::clamp(s2 + dk * 0.15f + lt * 0.15f, 0.0f, 1.0f) },
        { s3,   std::clamp(s3 + lt * 0.10f + hl * 0.20f, 0.0f, 1.0f) },
        { 1.0f, std::clamp(1.0f + hl * 0.15f, 0.70f, 1.0f) }
    };

    return eval_monotone_cubic_spline(param_pts, x);
}

static void recalculate_lut_from_current_mode() {
    if (g_curve_mode == CURVE_MODE_PARAMETRIC) {
        for (int i = 0; i < 256; ++i) {
            float x = i / 255.0f;
            lut_curve[i] = eval_parametric_curve(x);
        }
    } else {
        for (int i = 0; i < 256; ++i) {
            float x = i / 255.0f;
            lut_curve[i] = eval_monotone_cubic_spline(g_curve_points, x);
        }
    }
    lut_dirty = true;
}

void edit_lut_curve() {
    ImGui::Separator();

    // En-tête : "Régler :" + Sélecteurs de canaux R, G, B et Combiné + Icône de mode actif
    float width = ImGui::GetContentRegionAvail().x;

    ImGui::TextDisabled("☉");
    ImGui::SameLine(30.0f);
    ImGui::TextDisabled("Régler :");
    ImGui::SameLine();

    // Bouton Mode Paramétrique / Points (style Lightroom avec le point en-dessous si actif)
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
    if (ImGui::Button(g_curve_mode == CURVE_MODE_PARAMETRIC ? "〰" : "⚫", ImVec2(24, 22))) {
        g_curve_mode = (g_curve_mode == CURVE_MODE_PARAMETRIC) ? CURVE_MODE_POINT : CURVE_MODE_PARAMETRIC;
        recalculate_lut_from_current_mode();
        if (auto_apply_lut) apply_lut(lut_curve);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Basculer entre Courbe Paramétrique et Courbe à Points");
    ImGui::PopStyleVar();

    ImGui::SameLine();
    // Canal Combiné (RGB - Blanc)
    ImGui::PushStyleColor(ImGuiCol_Button, (g_selected_channel == 0) ? ImVec4(0.5f, 0.55f, 0.6f, 0.8f) : ImVec4(0.2f, 0.22f, 0.25f, 0.4f));
    if (ImGui::Button("⚪", ImVec2(24, 22))) { g_selected_channel = 0; recalculate_lut_from_current_mode(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Canal RVB combiné");
    ImGui::PopStyleColor();

    ImGui::SameLine();
    // Canal Rouge
    ImGui::PushStyleColor(ImGuiCol_Button, (g_selected_channel == 1) ? ImVec4(0.85f, 0.25f, 0.25f, 0.85f) : ImVec4(0.35f, 0.12f, 0.12f, 0.4f));
    if (ImGui::Button("🔴", ImVec2(24, 22))) { g_selected_channel = 1; recalculate_lut_from_current_mode(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Canal Rouge");
    ImGui::PopStyleColor();

    ImGui::SameLine();
    // Canal Vert
    ImGui::PushStyleColor(ImGuiCol_Button, (g_selected_channel == 2) ? ImVec4(0.25f, 0.85f, 0.25f, 0.85f) : ImVec4(0.12f, 0.35f, 0.12f, 0.4f));
    if (ImGui::Button("🟢", ImVec2(24, 22))) { g_selected_channel = 2; recalculate_lut_from_current_mode(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Canal Vert");
    ImGui::PopStyleColor();

    ImGui::SameLine();
    // Canal Bleu
    ImGui::PushStyleColor(ImGuiCol_Button, (g_selected_channel == 3) ? ImVec4(0.25f, 0.45f, 0.95f, 0.85f) : ImVec4(0.12f, 0.20f, 0.45f, 0.4f));
    if (ImGui::Button("🔵", ImVec2(24, 22))) { g_selected_channel = 3; recalculate_lut_from_current_mode(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Canal Bleu");
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Zone Graphique Carrée (Style Lightroom)
    float height = std::min(width, 260.0f);
    ImVec2 canvas_size(width, height);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::InvisibleButton("##ToneCurveCanvas", canvas_size);
    bool is_hovered = ImGui::IsItemHovered();
    ImVec2 rect_min = ImGui::GetItemRectMin();
    ImVec2 rect_max = ImGui::GetItemRectMax();
    ImGui::PopStyleVar();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Fond du canevas
    draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(24, 26, 30, 255), 4.0f);
    draw_list->AddRect(rect_min, rect_max, IM_COL32(60, 65, 75, 255), 4.0f, 0, 1.5f);

    // Rendu de la silhouette d'HISTOGRAMME en arrière-plan (Style Lightroom)
    const int histo_samples = 64;
    std::vector<ImVec2> histo_poly;
    histo_poly.reserve(histo_samples + 3);
    histo_poly.push_back(ImVec2(rect_min.x, rect_max.y));

    for (int i = 0; i <= histo_samples; ++i) {
        float t = (float)i / histo_samples;
        float h_val = 0.12f * std::sin(t * 3.14159f) +
                      0.38f * std::exp(-((t - 0.78f) * (t - 0.78f)) / 0.015f) +
                      0.18f * std::exp(-((t - 0.22f) * (t - 0.22f)) / 0.010f);
        h_val = std::clamp(h_val, 0.02f, 0.88f);
        histo_poly.push_back(ImVec2(rect_min.x + t * width, rect_max.y - h_val * height));
    }
    histo_poly.push_back(ImVec2(rect_max.x, rect_max.y));

    ImU32 histo_col = (g_selected_channel == 1) ? IM_COL32(90, 32, 32, 160) :
                      (g_selected_channel == 2) ? IM_COL32(32, 85, 32, 160) :
                      (g_selected_channel == 3) ? IM_COL32(32, 55, 105, 160) : IM_COL32(52, 56, 64, 170);

    draw_list->AddConvexPolyFilled(histo_poly.data(), (int)histo_poly.size(), histo_col);

    float s1_x = rect_min.x + g_split_shadows_darks * width;
    float s2_x = rect_min.x + g_split_darks_lights * width;
    float s3_x = rect_min.x + g_split_lights_highlights * width;

    // Surbrillance dynamique de région
    ImVec2 mouse_pos = ImGui::GetMousePos();
    float norm_mouse_x = std::clamp((mouse_pos.x - rect_min.x) / width, 0.0f, 1.0f);

    if (is_hovered && g_curve_mode == CURVE_MODE_PARAMETRIC) {
        if (norm_mouse_x < g_split_shadows_darks) g_hovered_tone_region = 1;
        else if (norm_mouse_x < g_split_darks_lights) g_hovered_tone_region = 2;
        else if (norm_mouse_x < g_split_lights_highlights) g_hovered_tone_region = 3;
        else g_hovered_tone_region = 4;
    }

    if (g_curve_mode == CURVE_MODE_PARAMETRIC && g_hovered_tone_region > 0) {
        float rx_min = rect_min.x, rx_max = rect_max.x;
        if (g_hovered_tone_region == 1) { rx_min = rect_min.x; rx_max = s1_x; }
        else if (g_hovered_tone_region == 2) { rx_min = s1_x; rx_max = s2_x; }
        else if (g_hovered_tone_region == 3) { rx_min = s2_x; rx_max = s3_x; }
        else if (g_hovered_tone_region == 4) { rx_min = s3_x; rx_max = rect_max.x; }

        draw_list->AddRectFilled(
            ImVec2(rx_min, rect_min.y),
            ImVec2(rx_max, rect_max.y),
            IM_COL32(255, 255, 255, 18)
        );
    }

    // Grille 4x4 fine
    for (int i = 1; i < 4; ++i) {
        float gx = rect_min.x + (width * i / 4.0f);
        float gy = rect_min.y + (height * i / 4.0f);
        draw_list->AddLine(ImVec2(gx, rect_min.y), ImVec2(gx, rect_max.y), IM_COL32(65, 70, 80, 100), 1.0f);
        draw_list->AddLine(ImVec2(rect_min.x, gy), ImVec2(rect_max.x, gy), IM_COL32(65, 70, 80, 100), 1.0f);
    }

    // Lignes verticales des séparateurs
    if (g_curve_mode == CURVE_MODE_PARAMETRIC) {
        draw_list->AddLine(ImVec2(s1_x, rect_min.y), ImVec2(s1_x, rect_max.y), IM_COL32(90, 100, 115, 140), 1.0f);
        draw_list->AddLine(ImVec2(s2_x, rect_min.y), ImVec2(s2_x, rect_max.y), IM_COL32(90, 100, 115, 140), 1.0f);
        draw_list->AddLine(ImVec2(s3_x, rect_min.y), ImVec2(s3_x, rect_max.y), IM_COL32(90, 100, 115, 140), 1.0f);
    }

    // Diagonale blanche neutre y = x (Identique à la capture)
    draw_list->AddLine(
        ImVec2(rect_min.x, rect_max.y),
        ImVec2(rect_max.x, rect_min.y),
        IM_COL32(230, 235, 245, 220),
        1.8f
    );

    // Traitement des points en Mode Point
    float norm_mouse_y = std::clamp(1.0f - (mouse_pos.y - rect_min.y) / height, 0.0f, 1.0f);

    int display_entry = 0;
    int display_exit = 0;

    if (g_curve_mode == CURVE_MODE_POINT) {
        float handle_radius = 6.0f;
        int hover_idx = -1;

        for (size_t i = 0; i < g_curve_points.size(); ++i) {
            ImVec2 pt_canvas(
                rect_min.x + g_curve_points[i].x * width,
                rect_max.y - g_curve_points[i].y * height
            );
            float dist_sq = (mouse_pos.x - pt_canvas.x) * (mouse_pos.x - pt_canvas.x) +
                            (mouse_pos.y - pt_canvas.y) * (mouse_pos.y - pt_canvas.y);
            if (dist_sq <= (handle_radius + 4.0f) * (handle_radius + 4.0f)) {
                hover_idx = (int)i;
                break;
            }
        }

        if (is_hovered && ImGui::IsMouseClicked(0)) {
            if (hover_idx >= 0) {
                g_dragged_point = hover_idx;
                g_selected_point = hover_idx;
            } else {
                g_selected_point = -1;
            }
        }

        if (is_hovered && ImGui::IsMouseDoubleClicked(0) && hover_idx < 0) {
            ToneCurvePoint new_pt{ norm_mouse_x, norm_mouse_y };
            auto it = std::lower_bound(g_curve_points.begin(), g_curve_points.end(), new_pt,
                [](const ToneCurvePoint& a, const ToneCurvePoint& b) { return a.x < b.x; });
            int new_idx = (int)std::distance(g_curve_points.begin(), it);
            g_curve_points.insert(it, new_pt);
            g_selected_point = new_idx;
            g_dragged_point = new_idx;
            g_point_curve_preset = 0;
            recalculate_lut_from_current_mode();
            if (auto_apply_lut) apply_lut(lut_curve);
        }

        if (is_hovered && ImGui::IsMouseClicked(1) && hover_idx > 0 && hover_idx < (int)g_curve_points.size() - 1) {
            g_curve_points.erase(g_curve_points.begin() + hover_idx);
            g_selected_point = -1;
            g_dragged_point = -1;
            g_point_curve_preset = 0;
            recalculate_lut_from_current_mode();
            if (auto_apply_lut) apply_lut(lut_curve);
        }

        if (g_dragged_point >= 0) {
            if (ImGui::IsMouseDown(0)) {
                int idx = g_dragged_point;
                int n = (int)g_curve_points.size();

                float min_x = (idx == 0) ? 0.0f : g_curve_points[idx - 1].x + 0.01f;
                float max_x = (idx == n - 1) ? 1.0f : g_curve_points[idx + 1].x - 0.01f;

                if (idx == 0) norm_mouse_x = 0.0f;
                if (idx == n - 1) norm_mouse_x = 1.0f;

                g_curve_points[idx].x = std::clamp(norm_mouse_x, min_x, max_x);
                g_curve_points[idx].y = norm_mouse_y;

                g_point_curve_preset = 0;
                recalculate_lut_from_current_mode();
                if (auto_apply_lut) apply_lut(lut_curve);
            } else {
                g_dragged_point = -1;
            }
        }

        // Calcul des valeurs d'entrée/sortie à afficher (Image 2 : 67 / 37)
        int target_idx = (g_selected_point >= 0 && g_selected_point < (int)g_curve_points.size()) ? g_selected_point :
                         (hover_idx >= 0) ? hover_idx : 1;
        if (target_idx >= 0 && target_idx < (int)g_curve_points.size()) {
            display_entry = (int)(g_curve_points[target_idx].x * 100.0f + 0.5f);
            display_exit = (int)(g_curve_points[target_idx].y * 100.0f + 0.5f);
        }
    }

    // Rendu de la courbe de réponse
    const int curve_segments = 128;
    std::vector<ImVec2> polyline_pts;
    polyline_pts.reserve(curve_segments + 1);

    for (int i = 0; i <= curve_segments; ++i) {
        float x = (float)i / curve_segments;
        float y = (g_curve_mode == CURVE_MODE_PARAMETRIC) ? eval_parametric_curve(x)
                                                          : eval_monotone_cubic_spline(g_curve_points, x);
        polyline_pts.push_back(ImVec2(
            rect_min.x + x * width,
            rect_max.y - y * height
        ));
    }

    ImU32 curve_col = (g_selected_channel == 1) ? IM_COL32(255, 80, 80, 255) :
                      (g_selected_channel == 2) ? IM_COL32(80, 240, 80, 255) :
                      (g_selected_channel == 3) ? IM_COL32(80, 160, 255, 255) : IM_COL32(240, 245, 250, 255);

    draw_list->AddPolyline(polyline_pts.data(), (int)polyline_pts.size(), curve_col, 0, 2.2f);

    // Dessin des nœuds et flèches de glissement verticales en Mode Point (Capture Lightroom Image 2)
    if (g_curve_mode == CURVE_MODE_POINT) {
        // Affichage des coordonnées X / Y en haut à gauche du graphique (ex: 67 / 37)
        char read_buf[32];
        std::snprintf(read_buf, sizeof(read_buf), "%d / %d", display_entry, display_exit);
        draw_list->AddText(ImVec2(rect_min.x + 8.0f, rect_min.y + 8.0f), IM_COL32(220, 225, 235, 220), read_buf);

        for (size_t i = 0; i < g_curve_points.size(); ++i) {
            ImVec2 pt_canvas(
                rect_min.x + g_curve_points[i].x * width,
                rect_max.y - g_curve_points[i].y * height
            );
            bool is_selected = ((int)i == g_selected_point);
            draw_list->AddCircleFilled(pt_canvas, is_selected ? 7.0f : 5.0f, IM_COL32(255, 255, 255, 255));
            draw_list->AddCircle(pt_canvas, (is_selected ? 7.0f : 5.0f) + 1.0f, IM_COL32(10, 15, 20, 255), 0, 1.5f);

            // Flèche bidirectionnelle verticale ↕ sur le point sélectionné ou glissé (Capture Image 2)
            if (is_selected || (int)i == g_dragged_point) {
                float px = pt_canvas.x;
                float py = pt_canvas.y;
                ImVec2 tri_top[3] = { ImVec2(px, py - 14.0f), ImVec2(px - 4.0f, py - 9.0f), ImVec2(px + 4.0f, py - 9.0f) };
                ImVec2 tri_bot[3] = { ImVec2(px, py + 14.0f), ImVec2(px - 4.0f, py + 9.0f), ImVec2(px + 4.0f, py + 9.0f) };
                draw_list->AddTriangleFilled(tri_top[0], tri_top[1], tri_top[2], IM_COL32(255, 255, 255, 240));
                draw_list->AddTriangleFilled(tri_bot[0], tri_bot[1], tri_bot[2], IM_COL32(255, 255, 255, 240));
            }
        }
    }

    // --- CONTRÔLES DU BAS EN FONCTION DU MODE (Image 1 vs Image 2) ---
    if (g_curve_mode == CURVE_MODE_PARAMETRIC) {
        // Barre à 3 curseurs sous le graphique
        float bar_y = rect_max.y + 4.0f;
        float bar_h = 10.0f;
        ImVec2 bar_min(rect_min.x, bar_y);
        ImVec2 bar_max(rect_max.x, bar_y + bar_h);

        draw_list->AddRectFilled(bar_min, bar_max, IM_COL32(35, 38, 44, 255), 3.0f);
        draw_list->AddRect(bar_min, bar_max, IM_COL32(70, 75, 85, 255), 3.0f);

        float split_vals[3] = { g_split_shadows_darks, g_split_darks_lights, g_split_lights_highlights };

        for (int k = 0; k < 3; ++k) {
            float k_x = rect_min.x + split_vals[k] * width;
            ImVec2 tri_pts[3] = {
                ImVec2(k_x - 5.0f, bar_y + bar_h + 3.0f),
                ImVec2(k_x + 5.0f, bar_y + bar_h + 3.0f),
                ImVec2(k_x, bar_y - 1.0f)
            };
            draw_list->AddTriangleFilled(tri_pts[0], tri_pts[1], tri_pts[2], IM_COL32(210, 215, 225, 255));
            draw_list->AddTriangle(tri_pts[0], tri_pts[1], tri_pts[2], IM_COL32(20, 25, 30, 255), 1.0f);
        }

        if (ImGui::IsMouseDown(0)) {
            if (g_dragged_split < 0 && mouse_pos.y >= bar_y - 4.0f && mouse_pos.y <= bar_y + bar_h + 8.0f) {
                for (int k = 0; k < 3; ++k) {
                    float k_x = rect_min.x + split_vals[k] * width;
                    if (std::abs(mouse_pos.x - k_x) <= 10.0f) {
                        g_dragged_split = k;
                        break;
                    }
                }
            }
            if (g_dragged_split >= 0) {
                float norm_x = std::clamp((mouse_pos.x - rect_min.x) / width, 0.05f, 0.95f);
                if (g_dragged_split == 0) g_split_shadows_darks = std::clamp(norm_x, 0.05f, g_split_darks_lights - 0.05f);
                else if (g_dragged_split == 1) g_split_darks_lights = std::clamp(norm_x, g_split_shadows_darks + 0.05f, g_split_lights_highlights - 0.05f);
                else if (g_dragged_split == 2) g_split_lights_highlights = std::clamp(norm_x, g_split_darks_lights + 0.05f, 0.95f);
                recalculate_lut_from_current_mode();
                if (auto_apply_lut) apply_lut(lut_curve);
            }
        } else {
            g_dragged_split = -1;
        }

        ImGui::Dummy(ImVec2(width, bar_h + 12.0f));

        // SECTION "Région" (Capture Image 1)
        ImGui::Spacing();
        float avail_w = ImGui::GetContentRegionAvail().x;
        ImVec2 title_sz = ImGui::CalcTextSize("Région");
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - title_sz.x) * 0.5f);
        ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.82f, 1.0f), "Région");
        ImGui::Spacing();

        bool changed = false;

        auto render_lightroom_region_slider = [&](const char* label, float* val, int region_id) {
            ImGui::Text("%-16s", label);
            if (ImGui::IsItemHovered()) g_hovered_tone_region = region_id;

            ImGui::SameLine(140.0f);
            float slider_w = avail_w - 200.0f;
            ImGui::SetNextItemWidth(std::max(100.0f, slider_w));
            std::string id = std::string("##") + label;
            if (ImGui::SliderFloat(id.c_str(), val, -100.0f, 100.0f, "")) {
                changed = true;
            }
            if (ImGui::IsItemHovered()) g_hovered_tone_region = region_id;

            ImGui::SameLine(avail_w - 35.0f);
            ImGui::Text("%4.0f", *val);
        };

        render_lightroom_region_slider("Hautes lumières", &g_param_highlights, 4);
        render_lightroom_region_slider("Tons clairs", &g_param_lights, 3);
        render_lightroom_region_slider("Tons sombres", &g_param_darks, 2);
        render_lightroom_region_slider("Ombres", &g_param_shadows, 1);

        if (!ImGui::IsAnyItemHovered() && !is_hovered) {
            g_hovered_tone_region = 0;
        }

        if (changed) {
            recalculate_lut_from_current_mode();
            if (auto_apply_lut) apply_lut(lut_curve);
        }
    } else {
        // CONTRÔLES DU MODE POINT (Capture Image 2)
        ImGui::Spacing();
        float avail_w = ImGui::GetContentRegionAvail().x;

        // Slider "Affiner la saturation" (ex: 27 sur la capture Image 2)
        ImGui::Text("Affiner la saturation");
        ImGui::SameLine(160.0f);
        ImGui::SetNextItemWidth(avail_w - 220.0f);
        if (ImGui::SliderFloat("##AffinerSaturation", &g_refine_saturation, 0.0f, 100.0f, "")) {
            // Mise à jour de la saturation
        }
        ImGui::SameLine(avail_w - 35.0f);
        ImGui::Text("%4.0f", g_refine_saturation);

        ImGui::Spacing();
        // Affichage numérique : Entrée : 67    Sortie : 37
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_w * 0.15f);
        ImGui::Text("Entrée :  %-4d     Sortie :  %-4d", display_entry, display_exit);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Menu Déroulant / Combo : Courbe à points : [ Personnalisé(e) ▼ ] (Capture Image 2)
        const char* point_curve_presets[] = { "Personnalisé(e)", "Linéaire", "Contraste moyen", "Contraste fort" };
        ImGui::Text("Courbe à points :");
        ImGui::SameLine(140.0f);
        ImGui::SetNextItemWidth(avail_w - 150.0f);
        if (ImGui::Combo("##PointCurvePresetCombo", &g_point_curve_preset, point_curve_presets, 4)) {
            if (g_point_curve_preset == 1) { // Linéaire
                g_curve_points = { {0.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 1.0f} };
            } else if (g_point_curve_preset == 2) { // Contraste moyen
                g_curve_points = { {0.0f, 0.0f}, {0.25f, 0.18f}, {0.5f, 0.50f}, {0.75f, 0.82f}, {1.0f, 1.0f} };
            } else if (g_point_curve_preset == 3) { // Contraste fort
                g_curve_points = { {0.0f, 0.0f}, {0.25f, 0.12f}, {0.5f, 0.50f}, {0.75f, 0.88f}, {1.0f, 1.0f} };
            }
            g_selected_point = -1;
            recalculate_lut_from_current_mode();
            if (auto_apply_lut) apply_lut(lut_curve);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Application en temps réel", &auto_apply_lut);
    ImGui::SameLine(width - 180.0f);
    if (ImGui::Button("🔄 Réinitialiser la Courbe")) {
        g_param_highlights = 0.0f; g_param_lights = 0.0f; g_param_darks = 0.0f; g_param_shadows = 0.0f;
        g_split_shadows_darks = 0.25f; g_split_darks_lights = 0.50f; g_split_lights_highlights = 0.75f;
        g_curve_points = { {0.0f, 0.0f}, {0.67f, 0.37f}, {1.0f, 1.0f} };
        g_selected_point = 1;
        g_refine_saturation = 27.0f;
        g_point_curve_preset = 0;
        recalculate_lut_from_current_mode();
        apply_lut(lut_curve);
    }
}

// --- GESTION DU MODE ACES DISPLAY (DaVinci Resolve / NobeDisplay Connect) ---

static float eval_aces_rrt_odt(float x) {
    // Courbe RRT/ODT ACES (Ajustement Stephen Hill)
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    float val = (x * (a * x + b)) / (x * (c * x + d) + e);
    return std::clamp(val, 0.0f, 1.0f);
}

static void kelvin_to_rgb_gains(float kelvin, float& r, float& g, float& b) {
    float temp = kelvin / 100.0f;
    if (temp <= 66.0f) {
        r = 1.0f;
        g = std::clamp((99.4708025861f * std::log(temp) - 161.1195681661f) / 255.0f, 0.1f, 1.0f);
        b = (temp <= 19.0f) ? 0.0f : std::clamp((138.5177312231f * std::log(temp - 10.0f) - 305.0447927307f) / 255.0f, 0.0f, 1.0f);
    } else {
        r = std::clamp((329.698727446f * std::pow(temp - 60.0f, -0.1332047592f)) / 255.0f, 0.1f, 1.0f);
        g = std::clamp((288.1221695283f * std::pow(temp - 60.0f, -0.0755148492f)) / 255.0f, 0.1f, 1.0f);
        b = 1.0f;
    }
}

void apply_aces_settings() {
    if (!g_config.aces_enabled) return;

    // Multiplicateur d'exposition EV (2^EV)
    float exp_scale = std::pow(2.0f, g_config.aces_exposure);

    // Gains de température de couleur (Bradford adaptation)
    float r_gain = 1.0f, g_gain = 1.0f, b_gain = 1.0f;
    kelvin_to_rgb_gains(g_config.aces_color_temp, r_gain, g_gain, b_gain);

    // Effet de la teinte Tint (-50 à +50: vert à magenta)
    float tint_factor = g_config.aces_tint / 100.0f;
    g_gain = std::clamp(g_gain * (1.0f - tint_factor), 0.1f, 2.0f);
    b_gain = std::clamp(b_gain * (1.0f + tint_factor * 0.5f), 0.1f, 2.0f);
    r_gain = std::clamp(r_gain * (1.0f + tint_factor * 0.5f), 0.1f, 2.0f);

    // Calcul de la table de réponse LUT 256 niveaux via la courbe ACES RRT/ODT + DRX/Shadow/Brightness
    for (int i = 0; i < 256; ++i) {
        float norm_in = i / 255.0f;

        // 1. Exposure & Brightness offset
        float val = norm_in * exp_scale + g_config.aces_brightness * 0.2f;
        val = std::max(0.0f, val);

        // 2. Shadow lift
        if (g_config.aces_shadow != 0.0f) {
            float shadow_lift = g_config.aces_shadow * 0.25f;
            float weight = (1.0f - std::min(1.0f, val));
            val += shadow_lift * weight * weight;
        }

        // 3. Evaluation ACES RRT/ODT
        float aces_out = eval_aces_rrt_odt(val);

        // 4. DRX (Dynamic Range Extension / Knee Highlight Recovery)
        if (g_config.aces_drx > 0.0f) {
            float knee = 0.75f;
            if (aces_out > knee) {
                float excess = aces_out - knee;
                float rolled_off = excess / (1.0f + g_config.aces_drx * excess * 2.0f);
                aces_out = knee + rolled_off;
            }
        }

        lut_curve[i] = std::clamp(aces_out, 0.0f, 1.0f);
    }
    lut_dirty = true;

    // Appliquer les gains de couleur et la LUT
    g_config.rgb[0] = r_gain;
    g_config.rgb[1] = g_gain;
    g_config.rgb[2] = b_gain;

    apply_lut(lut_curve);
}

void render_aces_controls() {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1.0f), "🎬 Réglages ACES Display (Gestion de la Couleur & Color Science)");
    ImGui::TextDisabled("Configuration de la chaîne de gestion colorimétrique ACES style DaVinci Resolve / NobeDisplay.");

    bool aces_changed = false;

    if (ImGui::Checkbox("Activer le Mode ACES Display", &g_config.aces_enabled)) {
        aces_changed = true;
    }

    if (!g_config.aces_enabled) {
        ImGui::TextDisabled("Activez la case ci-dessus pour utiliser le système d'encodage de couleur ACES.");
        return;
    }

    ImGui::Spacing();
    float avail_w = ImGui::GetContentRegionAvail().x;

    // 1. Color Science
    const char* color_sciences[] = { "ACEScct v1.3", "ACEScc v1.3", "ACEScg (VFX)", "ACES2065-1 (Archival)", "DaVinci YRGB Color Managed" };
    ImGui::Text("Color Science :");
    ImGui::SameLine(180.0f);
    ImGui::SetNextItemWidth(avail_w - 190.0f);
    if (ImGui::Combo("##ACES_ColorScience", &g_config.aces_color_science, color_sciences, 5)) aces_changed = true;

    // 2. IDT (Espace Couleur Entrée)
    const char* input_spaces[] = {
        "Rec.709 / sRGB",
        "Adobe RGB (1998)",
        "Apple RGB (Mac Classic Gamma 1.8)",
        "Bruce RGB (Gamma 2.2 - D65)",
        "ProPhoto RGB / ROMM (Gamma 1.8 - D50)",
        "ColorMatch RGB (Gamma 1.8 - D50)",
        "NTSC RGB (Gamma 2.2 - White C)",
        "PAL/SECAM RGB (Gamma 2.2 - D65)",
        "Wide Gamut RGB (Gamma 2.2 - D50)",
        "REDWideGamutRGB / REDlogFilm",
        "ARRI Alexa LogC3",
        "Sony S-Gamut3.Cine / S-Log3",
        "DCI-P3 (Cinéma)"
    };
    ImGui::Text("Espace Entrée (IDT) :");
    ImGui::SameLine(180.0f);
    ImGui::SetNextItemWidth(avail_w - 190.0f);
    if (ImGui::Combo("##ACES_IDT", &g_config.aces_input_space, input_spaces, 13)) aces_changed = true;

    // 3. ODT (Transformée de Sortie Écran)
    const char* output_transforms[] = {
        "Rec.709 (Gamma 2.4 - 100 nits)",
        "sRGB (Moniteur PC)",
        "Adobe RGB (1998) (Gamma 2.2)",
        "Apple RGB (Mac Classic Gamma 1.8)",
        "Bruce RGB (Gamma 2.2 - D65)",
        "ProPhoto RGB / ROMM (Gamma 1.8 - D50)",
        "ColorMatch RGB (Gamma 1.8 - D50)",
        "NTSC RGB (Gamma 2.2 - White C)",
        "PAL/SECAM RGB (Gamma 2.2 - D65)",
        "Wide Gamut RGB (Gamma 2.2 - D50)",
        "DCI-P3 (Projecteur Cinéma)",
        "Rec.2020 PQ HDR (1000 nits)",
        "Rec.2020 HLG HDR"
    };
    ImGui::Text("Sortie Écran (ODT) :");
    ImGui::SameLine(180.0f);
    ImGui::SetNextItemWidth(avail_w - 190.0f);
    if (ImGui::Combo("##ACES_ODT", &g_config.aces_output_transform, output_transforms, 13)) aces_changed = true;

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.85f, 0.9f, 0.95f, 1.0f), "🎛️ Régleurs ACES Raw Controls (Conforme à l'image de référence) :");

    // Helper lambda pour afficher Slider + InputFloat côte à côte (Style DaVinci / RED RAW)
    auto render_raw_row = [&](const char* label_name, const char* id_str, float* val_ptr, float min_v, float max_v, float step, const char* format_str) -> bool {
        bool local_changed = false;
        ImGui::Text("%-12s", label_name);
        ImGui::SameLine(130.0f);
        ImGui::SetNextItemWidth(avail_w - 240.0f);
        if (ImGui::SliderFloat((std::string("##S_") + id_str).c_str(), val_ptr, min_v, max_v, "")) local_changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::InputFloat((std::string("##I_") + id_str).c_str(), val_ptr, step, step * 10.0f, format_str)) local_changed = true;
        *val_ptr = std::clamp(*val_ptr, min_v, max_v);
        return local_changed;
    };

    if (render_raw_row("Color Temp", "ColorTemp", &g_config.aces_color_temp, 2000.0f, 12000.0f, 50.0f, "%.0f")) aces_changed = true;
    if (render_raw_row("Tint", "Tint", &g_config.aces_tint, -50.0f, 50.0f, 0.5f, "%.2f")) aces_changed = true;
    if (render_raw_row("DRX", "DRX", &g_config.aces_drx, 0.0f, 1.0f, 0.01f, "%.2f")) aces_changed = true;
    if (render_raw_row("Shadow", "Shadow", &g_config.aces_shadow, -1.0f, 1.0f, 0.01f, "%.2f")) aces_changed = true;
    if (render_raw_row("Exposure", "Exposure", &g_config.aces_exposure, -5.0f, 5.0f, 0.05f, "%.2f")) aces_changed = true;
    if (render_raw_row("Brightness", "Brightness", &g_config.aces_brightness, -1.0f, 1.0f, 0.01f, "%.2f")) aces_changed = true;

    // Visualisation de la Courbe RRT/ODT ACES
    ImGui::Spacing();
    ImGui::TextDisabled("Courbe de Réponse ACES RRT/ODT (S-Curve Tonemapper) :");

    float canvas_h = 140.0f;
    ImVec2 canvas_sz(avail_w, canvas_h);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::InvisibleButton("##ACESToneCanvas", canvas_sz);
    ImVec2 c_min = ImGui::GetItemRectMin();
    ImVec2 c_max = ImGui::GetItemRectMax();
    ImGui::PopStyleVar();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(c_min, c_max, IM_COL32(20, 22, 28, 255), 4.0f);
    dl->AddRect(c_min, c_max, IM_COL32(60, 65, 75, 255), 4.0f);

    // Grille 4x4
    for (int i = 1; i < 4; ++i) {
        float gx = c_min.x + (avail_w * i / 4.0f);
        float gy = c_min.y + (canvas_h * i / 4.0f);
        dl->AddLine(ImVec2(gx, c_min.y), ImVec2(gx, c_max.y), IM_COL32(50, 55, 65, 120), 1.0f);
        dl->AddLine(ImVec2(c_min.x, gy), ImVec2(c_max.x, gy), IM_COL32(50, 55, 65, 120), 1.0f);
    }

    // Traceur de la courbe S ACES
    float exp_scale = std::pow(2.0f, g_config.aces_exposure);
    const int segments = 100;
    std::vector<ImVec2> pts;
    pts.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        float t = (float)i / segments;
        float val = eval_aces_rrt_odt(t * exp_scale);
        pts.push_back(ImVec2(c_min.x + t * avail_w, c_max.y - val * canvas_h));
    }
    dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(245, 170, 40, 255), 0, 2.0f);

    if (aces_changed) {
        apply_aces_settings();
    }

    ImGui::Spacing();
    if (ImGui::Button("Appliquer Configuration ACES")) {
        apply_aces_settings();
    }
    ImGui::SameLine();
    if (ImGui::Button("🔄 Réinitialiser ACES")) {
        g_config.aces_exposure = 0.0f;
        g_config.aces_color_temp = 6500.0f;
        g_config.aces_tint = 0.0f;
        g_config.aces_drx = 0.0f;
        g_config.aces_shadow = 0.0f;
        g_config.aces_brightness = 0.0f;
        g_config.aces_color_science = 0;
        g_config.aces_input_space = 0;
        g_config.aces_output_transform = 0;
        apply_aces_settings();
    }
}

// --- ADAPTATION CHROMATIQUE (CAT - Chromatic Adaptation Transform) ---

struct IlluminantXYZ {
    const char* name;
    float x, y, z;
};

static const IlluminantXYZ g_illuminants[] = {
    { "D65 (6504K - Standard Vidéo)", 0.95047f, 1.00000f, 1.08883f },
    { "D50 (5003K - Prépresse ICC)", 0.96422f, 1.00000f, 0.82521f },
    { "D55 (5500K - Lumière du jour)", 0.95682f, 1.00000f, 0.92149f },
    { "Illuminant A (2856K - Tungstène)", 1.09850f, 1.00000f, 0.35585f },
    { "Illuminant C (6774K - Lumière nord)", 0.98074f, 1.00000f, 1.18232f },
    { "Illuminant E (5000K - Équi-énergie)", 1.00000f, 1.00000f, 1.00000f }
};

void apply_chromatic_adaptation() {
    int src_idx = std::clamp(g_config.cat_source_illuminant, 0, 5);
    int tgt_idx = std::clamp(g_config.cat_target_illuminant, 0, 5);
    int method = std::clamp(g_config.cat_method, 0, 4);

    IlluminantXYZ src = g_illuminants[src_idx];
    IlluminantXYZ tgt = g_illuminants[tgt_idx];

    // Matrice CAT M (3x3)
    float M[3][3];
    float M_inv[3][3];

    if (method == 0) { // Bradford
        float B[3][3] = {
            { 0.8951f,  0.2664f, -0.1614f},
            {-0.7502f,  1.7135f,  0.0367f},
            { 0.0389f, -0.0685f,  1.0296f}
        };
        float Bi[3][3] = {
            { 0.986993f, -0.147054f,  0.159963f},
            { 0.432305f,  0.518360f,  0.049291f},
            {-0.008529f,  0.040043f,  0.968487f}
        };
        memcpy(M, B, sizeof(B));
        memcpy(M_inv, Bi, sizeof(Bi));
    } else if (method == 1) { // CAT02
        float C[3][3] = {
            { 0.7328f,  0.4296f, -0.1624f},
            {-0.7036f,  1.6975f,  0.0061f},
            { 0.0030f,  0.0136f,  0.9834f}
        };
        float Ci[3][3] = {
            { 1.096124f, -0.278869f,  0.182745f},
            { 0.454369f,  0.473533f,  0.072098f},
            {-0.009628f, -0.005698f,  1.015326f}
        };
        memcpy(M, C, sizeof(C));
        memcpy(M_inv, Ci, sizeof(Ci));
    } else if (method == 2) { // Von Kries
        float VK[3][3] = {
            { 0.40024f,  0.70760f, -0.08081f},
            {-0.22630f,  1.16532f,  0.04570f},
            { 0.00000f,  0.00000f,  0.91822f}
        };
        float VKi[3][3] = {
            { 1.859936f, -1.129382f,  0.219897f},
            { 0.361191f,  0.638812f, -0.000006f},
            { 0.000000f,  0.000000f,  1.089064f}
        };
        memcpy(M, VK, sizeof(VK));
        memcpy(M_inv, VKi, sizeof(VKi));
    } else if (method == 3) { // XYZ Scaling
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) { M[i][j] = (i==j)?1.0f:0.0f; M_inv[i][j] = (i==j)?1.0f:0.0f; }
    } else { // Sharp
        float S[3][3] = {
            { 1.2694f, -0.0988f, -0.1706f},
            {-0.8364f,  1.8006f,  0.0357f},
            { 0.0357f, -0.0315f,  0.9959f}
        };
        float Si[3][3] = {
            { 0.81563f,  0.04715f,  0.13802f},
            { 0.37911f,  0.57693f,  0.04396f},
            {-0.01726f,  0.01994f,  1.00164f}
        };
        memcpy(M, S, sizeof(S));
        memcpy(M_inv, Si, sizeof(Si));
    }

    // Cone responses
    float rho_s = M[0][0]*src.x + M[0][1]*src.y + M[0][2]*src.z;
    float gamma_s = M[1][0]*src.x + M[1][1]*src.y + M[1][2]*src.z;
    float beta_s = M[2][0]*src.x + M[2][1]*src.y + M[2][2]*src.z;

    float rho_t = M[0][0]*tgt.x + M[0][1]*tgt.y + M[0][2]*tgt.z;
    float gamma_t = M[1][0]*tgt.x + M[1][1]*tgt.y + M[1][2]*tgt.z;
    float beta_t = M[2][0]*tgt.x + M[2][1]*tgt.y + M[2][2]*tgt.z;

    float r_scale = rho_t / (rho_s > 1e-5f ? rho_s : 1.0f);
    float g_scale = gamma_t / (gamma_s > 1e-5f ? gamma_s : 1.0f);
    float b_scale = beta_t / (beta_s > 1e-5f ? beta_s : 1.0f);

    // M_adapt = M_inv * D * M
    float D_M[3][3];
    for (int j=0;j<3;++j) {
        D_M[0][j] = r_scale * M[0][j];
        D_M[1][j] = g_scale * M[1][j];
        D_M[2][j] = b_scale * M[2][j];
    }
    float M_adapt[3][3];
    for (int i=0;i<3;++i) {
        for (int j=0;j<3;++j) {
            M_adapt[i][j] = M_inv[i][0]*D_M[0][j] + M_inv[i][1]*D_M[1][j] + M_inv[i][2]*D_M[2][j];
        }
    }

    // Convert (1,1,1) white vector through adaptation matrix to get gain offsets
    float r_gain = std::clamp(M_adapt[0][0] + M_adapt[0][1] + M_adapt[0][2], 0.1f, 2.0f);
    float g_gain = std::clamp(M_adapt[1][0] + M_adapt[1][1] + M_adapt[1][2], 0.1f, 2.0f);
    float b_gain = std::clamp(M_adapt[2][0] + M_adapt[2][1] + M_adapt[2][2], 0.1f, 2.0f);

    g_config.rgb[0] = r_gain;
    g_config.rgb[1] = g_gain;
    g_config.rgb[2] = b_gain;

    apply_combined_gamma();
    set_status(true, "Adaptation chromatique appliquée avec succès");
}

void render_cat_controls() {
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.95f, 1.0f), "💡 Réglages Adaptation Chromatique (CAT - Chromatic Adaptation Transform)");
    ImGui::TextDisabled("Transformation d'adaptation entre illuminants / points blancs de référence (Bradford, CAT02, Von Kries).");

    bool cat_changed = false;

    float avail_w = ImGui::GetContentRegionAvail().x;

    // 1. Méthode CAT
    const char* cat_methods[] = { "Bradford (Standard ICC & ACES)", "CAT02 (CIE CAM02)", "Von Kries", "XYZ Scaling", "Sharp CAT" };
    ImGui::Text("Méthode CAT :");
    ImGui::SameLine(180.0f);
    ImGui::SetNextItemWidth(avail_w - 190.0f);
    if (ImGui::Combo("##CAT_Method", &g_config.cat_method, cat_methods, 5)) cat_changed = true;

    // 2. Illuminant Source
    const char* illuminants[] = {
        "D65 (6504K - Vidéo/Web Standard)",
        "D50 (5003K - Prépresse ICC)",
        "D55 (5500K - Lumière du jour)",
        "Illuminant A (2856K - Tungstène)",
        "Illuminant C (6774K - Lumière nord)",
        "Illuminant E (5000K - Équi-énergie)"
    };
    ImGui::Text("Illuminant Source :");
    ImGui::SameLine(180.0f);
    ImGui::SetNextItemWidth(avail_w - 190.0f);
    if (ImGui::Combo("##CAT_Source", &g_config.cat_source_illuminant, illuminants, 6)) cat_changed = true;

    // 3. Illuminant Cible
    ImGui::Text("Illuminant Cible :");
    ImGui::SameLine(180.0f);
    ImGui::SetNextItemWidth(avail_w - 190.0f);
    if (ImGui::Combo("##CAT_Target", &g_config.cat_target_illuminant, illuminants, 6)) cat_changed = true;

    if (cat_changed) {
        apply_chromatic_adaptation();
    }

    ImGui::Spacing();
    if (ImGui::Button("Appliquer Adaptation Chromatique")) {
        apply_chromatic_adaptation();
    }
    ImGui::SameLine();
    if (ImGui::Button("🔄 Réinitialiser CAT")) {
        g_config.cat_method = 0;
        g_config.cat_source_illuminant = 0;
        g_config.cat_target_illuminant = 0;
        apply_chromatic_adaptation();
    }
}

void apply_luminance_levels(float white_cdm2, float black_cdm2) {
    g_config.white_level_cdm2 = std::clamp(white_cdm2, 10.0f, 4000.0f);
    g_config.black_level_cdm2 = std::clamp(black_cdm2, 0.0f, 20.0f);

    float black_ratio = (g_config.white_level_cdm2 > 1e-3f) ? (g_config.black_level_cdm2 / g_config.white_level_cdm2) : 0.0f;
    black_ratio = std::clamp(black_ratio, 0.0f, 0.5f);

    for (int i = 0; i < 256; ++i) {
        float in_val = i / 255.0f;
        float out_val = black_ratio + (1.0f - black_ratio) * in_val;
        lut_curve[i] = out_val;
    }
    lut_dirty = true;

    apply_lut(lut_curve);
    set_status(true, "Niveau de blanc et niveau de noir appliqués avec succès");
}

void reset_all_display_settings() {
    // 1. Contrôle général (100% Clair par défaut)
    g_config.brightness = 300;
    g_config.contrast = 1.0f;
    g_config.gamma = 2.2f;
    g_config.rgb = {1.0f, 1.0f, 1.0f};
    g_config.cmyk = {0.0f, 0.0f, 0.0f, 0.0f};
    g_config.white = 1.0f;
    g_config.color_mode = 0; // sRGB

    // 2. ACES Display
    g_config.aces_enabled = false;
    g_config.aces_color_science = 0;
    g_config.aces_input_space = 0;
    g_config.aces_output_transform = 0;
    g_config.aces_exposure = 0.0f;
    g_config.aces_color_temp = 6500.0f;
    g_config.aces_tint = 0.0f;
    g_config.aces_drx = 0.0f;
    g_config.aces_shadow = 0.0f;
    g_config.aces_brightness = 0.0f;

    // 3. CAT (Adaptation Chromatique)
    g_config.cat_method = 0;
    g_config.cat_source_illuminant = 0;
    g_config.cat_target_illuminant = 0;

    // 4. Niveaux de blanc & noir
    g_config.white_level_cdm2 = 120.0f;
    g_config.black_level_cdm2 = 0.0f;

    // 5. Courbe de tonalité (Linéaire standard)
    g_param_highlights = 0.0f; g_param_lights = 0.0f; g_param_darks = 0.0f; g_param_shadows = 0.0f;
    g_split_shadows_darks = 0.25f; g_split_darks_lights = 0.50f; g_split_lights_highlights = 0.75f;
    g_curve_points = { {0.0f, 0.0f}, {0.67f, 0.37f}, {1.0f, 1.0f} };
    g_selected_point = 1;
    g_refine_saturation = 27.0f;
    g_point_curve_preset = 0;

    // 6. LUT 256 niveaux : Rampe d'identité linéaire (0.0 à 1.0)
    for (int i = 0; i < 256; ++i) {
        float val = i / 255.0f;
        g_config.lut[i] = val;
        lut_curve[i] = val;
    }
    lut_dirty = false;

    // 7. Application matérielle XRandR : Luminance 100%, Blanc 1.0, Noir 0.0 (Écran Clair & Net!)
    apply_brightness(300);
    apply_contrast(1.0f);
    apply_color_adjust(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f); // w=1.0f (white), k=0.0f (black ink)
    apply_lut(lut_curve);

    save_config(g_config);
    set_status(true, "Réinitialisation réussie : tout a été rétabli à 100% de clarté lumineuse par défaut.");
}

void save_current_config() {
    save_config(g_config);
}

static bool g_live_apply_all = true;

void render_settings() {
    ImGui::BeginChild("Settings", ImVec2(0, -50), true);

    // Bouton de Réinitialisation Générale Harmonisé (Défaut Usine)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.95f, 0.3f, 0.3f, 1.0f));
    if (ImGui::Button("🔄 Réinitialiser TOUS les Réglages d'Affichage (Défaut Usine)", ImVec2(ImGui::GetContentRegionAvail().x, 32.0f))) {
        reset_all_display_settings();
    }
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "--- Contrôle Général d'Image ---");
    ImGui::Checkbox("Application automatique des modifications en temps réel", &g_live_apply_all);
    ImGui::Separator();

    // Insertion du panneau ACES Display
    render_aces_controls();
    ImGui::Separator();

    // Insertion du panneau Adaptation Chromatique (CAT)
    render_cat_controls();
    ImGui::Separator();

    // --- NIVEAU DE BLANC & NIVEAU DE NOIR (cd/m²) ---
    ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1.0f), "☀️ Niveaux de Luminance Cible (Étalonnage Écran)");

    bool level_changed = false;

    // Niveau de blanc (cd/m²)
    ImGui::Text("Niveau de blanc");
    ImGui::SameLine(140.0f);
    ImGui::PushItemWidth(130.0f);
    if (ImGui::InputFloat("##WhiteLevelInput", &g_config.white_level_cdm2, 1.0f, 10.0f, "%.2f")) level_changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::Text("cd/m²");

    ImGui::SameLine(330.0f);

    // Niveau de noir (cd/m²)
    ImGui::Text("Niveau de noir");
    ImGui::SameLine(460.0f);
    ImGui::PushItemWidth(150.0f);
    if (ImGui::InputFloat("##BlackLevelInput", &g_config.black_level_cdm2, 0.001f, 0.01f, "%.6f")) level_changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::Text("cd/m²");

    if (level_changed) {
        g_config.white_level_cdm2 = std::clamp(g_config.white_level_cdm2, 10.0f, 4000.0f);
        g_config.black_level_cdm2 = std::clamp(g_config.black_level_cdm2, 0.0f, 20.0f);
        if (g_live_apply_all) apply_luminance_levels(g_config.white_level_cdm2, g_config.black_level_cdm2);
    }

    ImGui::Spacing();
    if (ImGui::Button("Appliquer Niveaux de Blanc / Noir")) {
        apply_luminance_levels(g_config.white_level_cdm2, g_config.black_level_cdm2);
    }
    ImGui::SameLine();
    if (ImGui::Button("🔄 Réinitialiser Niveaux (120 cd/m² / 0 cd/m²)")) {
        g_config.white_level_cdm2 = 120.0f;
        g_config.black_level_cdm2 = 0.0f;
        apply_luminance_levels(120.0f, 0.0f);
    }

    ImGui::Separator();

    // 1. LUMINANCE
    static int brightness = g_config.brightness;
    bool brightness_changed = false;
    ImGui::Text("Luminance (lux) : ");
    ImGui::PushItemWidth(250.0f);
    if (ImGui::SliderInt("##Luminance_slider", &brightness, 0, 1000)) brightness_changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(90.0f);
    if (ImGui::InputInt("##Luminance_input", &brightness, 10, 50)) brightness_changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Appliquer Luminance")) {
        g_config.brightness = brightness;
        apply_brightness(brightness);
    }

    brightness = std::clamp(brightness, 0, 1000);
    if (brightness_changed) {
        g_config.brightness = brightness;
        if (g_live_apply_all) apply_brightness(brightness);
    }

    // 2. CONTRASTE
    static float contrast = g_config.contrast;
    bool contrast_changed = false;
    ImGui::Text("Contraste : ");
    ImGui::PushItemWidth(250.0f);
    if (ImGui::SliderFloat("##Contrast_slider", &contrast, 0.1f, 2.0f, "%.2f")) contrast_changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(90.0f);
    if (ImGui::InputFloat("##Contrast_input", &contrast, 0.05f, 0.1f, "%.2f")) contrast_changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Appliquer Contraste")) {
        g_config.contrast = contrast;
        apply_contrast(contrast);
    }

    contrast = std::clamp(contrast, 0.1f, 2.0f);
    if (contrast_changed) {
        g_config.contrast = contrast;
        if (g_live_apply_all) apply_contrast(contrast);
    }

    // 3. OMBRES ET HAUTES LUMIÈRES
    static float shadows = 0.0f, highlights = 1.0f;
    bool sh_changed = false;
    ImGui::Text("Ombres (courbe bas) : ");
    ImGui::PushItemWidth(250.0f);
    if (ImGui::SliderFloat("##Shadows_slider", &shadows, 0.0f, 1.0f, "%.2f")) sh_changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(90.0f);
    if (ImGui::InputFloat("##Shadows_input", &shadows, 0.05f, 0.1f, "%.2f")) sh_changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Appliquer Ombres (courbe bas)")) {
        apply_shadows_highlights(shadows, highlights);
    }

    ImGui::Text("Hautes lumières (courbe haut) : ");
    ImGui::PushItemWidth(250.0f);
    if (ImGui::SliderFloat("##Highlights_slider", &highlights, 0.0f, 1.0f, "%.2f")) sh_changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(90.0f);
    if (ImGui::InputFloat("##Highlights_input", &highlights, 0.05f, 0.1f, "%.2f")) sh_changed = true;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Appliquer Hautes lumières (courbe haut)")) {
        apply_shadows_highlights(shadows, highlights);
    }

    shadows = std::clamp(shadows, 0.0f, 1.0f);
    highlights = std::clamp(highlights, 0.0f, 1.0f);
    if (sh_changed && g_live_apply_all) {
        apply_shadows_highlights(shadows, highlights);
    }

    // 4. ESPACES COLORIMÉTRIQUES (RVB, HSL, HSV, HSI, CMYK & BLANC)
    struct ColorState {
        float rgb[3] = {1.0f, 1.0f, 1.0f};
        float hsl[3] = {0.0f, 0.0f, 1.0f};
        float hsv[3] = {0.0f, 0.0f, 1.0f};
        float hsi[3] = {0.0f, 0.0f, 1.0f};
        float cmyk[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float white = 1.0f;
        bool initialized = false;
    };
    static ColorState cs;
    if (!cs.initialized) {
        for (int i=0; i<3; ++i) cs.rgb[i] = g_config.rgb[i];
        for (int i=0; i<4; ++i) cs.cmyk[i] = g_config.cmyk[i];
        cs.white = g_config.white;
        rgb_to_hsl(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsl[0], cs.hsl[1], cs.hsl[2]);
        rgb_to_hsv(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsv[0], cs.hsv[1], cs.hsv[2]);
        rgb_to_hsi(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsi[0], cs.hsi[1], cs.hsi[2]);
        cs.initialized = true;
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "--- Édition Numérique des Couleurs (RVB, HSL, HSV, HSI, CMYK & Blanc) ---");

    bool color_changed = false;

    if (ImGui::BeginTabBar("ColorSpaceTabs")) {
        if (ImGui::BeginTabItem("RVB (RGB)")) {
            if (ImGui::ColorEdit3("Palette RVB", cs.rgb)) color_changed = true;

            ImGui::Text("Rouge (R) : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##R_slider", &cs.rgb[0], 0.0f, 1.0f, "%.3f")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##R_input", &cs.rgb[0], 0.01f, 0.05f, "%.3f")) color_changed = true;
            ImGui::PopItemWidth();

            ImGui::Text("Vert (G) : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##G_slider", &cs.rgb[1], 0.0f, 1.0f, "%.3f")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##G_input", &cs.rgb[1], 0.01f, 0.05f, "%.3f")) color_changed = true;
            ImGui::PopItemWidth();

            ImGui::Text("Bleu (B) : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##B_slider", &cs.rgb[2], 0.0f, 1.0f, "%.3f")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##B_input", &cs.rgb[2], 0.01f, 0.05f, "%.3f")) color_changed = true;
            ImGui::PopItemWidth();

            if (color_changed) {
                cs.rgb[0] = std::clamp(cs.rgb[0], 0.0f, 1.0f);
                cs.rgb[1] = std::clamp(cs.rgb[1], 0.0f, 1.0f);
                cs.rgb[2] = std::clamp(cs.rgb[2], 0.0f, 1.0f);
                rgb_to_hsl(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsl[0], cs.hsl[1], cs.hsl[2]);
                rgb_to_hsv(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsv[0], cs.hsv[1], cs.hsv[2]);
                rgb_to_hsi(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsi[0], cs.hsi[1], cs.hsi[2]);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("HSL")) {
            ImGui::Text("Teinte HSL (°) : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##HSL_H_slider", &cs.hsl[0], 0.0f, 360.0f, "%.1f°")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##HSL_H_input", &cs.hsl[0], 1.0f, 10.0f, "%.1f°")) color_changed = true;
            ImGui::PopItemWidth();

            ImGui::Text("Saturation HSL : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##HSL_S_slider", &cs.hsl[1], 0.0f, 1.0f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##HSL_S_input", &cs.hsl[1], 0.01f, 0.05f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();

            ImGui::Text("Luminosité HSL : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##HSL_L_slider", &cs.hsl[2], 0.0f, 1.0f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##HSL_L_input", &cs.hsl[2], 0.01f, 0.05f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();

            if (color_changed) {
                cs.hsl[0] = fmodf(cs.hsl[0], 360.0f); if (cs.hsl[0] < 0) cs.hsl[0] += 360.0f;
                cs.hsl[1] = std::clamp(cs.hsl[1], 0.0f, 1.0f);
                cs.hsl[2] = std::clamp(cs.hsl[2], 0.0f, 1.0f);
                hsl_to_rgb(cs.hsl[0], cs.hsl[1], cs.hsl[2], cs.rgb[0], cs.rgb[1], cs.rgb[2]);
                rgb_to_hsv(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsv[0], cs.hsv[1], cs.hsv[2]);
                rgb_to_hsi(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsi[0], cs.hsi[1], cs.hsi[2]);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("HSV")) {
            ImGui::Text("Teinte HSV (°) : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##HSV_H_slider", &cs.hsv[0], 0.0f, 360.0f, "%.1f°")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##HSV_H_input", &cs.hsv[0], 1.0f, 10.0f, "%.1f°")) color_changed = true;
            ImGui::PopItemWidth();

            ImGui::Text("Saturation HSV : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##HSV_S_slider", &cs.hsv[1], 0.0f, 1.0f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##HSV_S_input", &cs.hsv[1], 0.01f, 0.05f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();

            ImGui::Text("Valeur HSV : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##HSV_V_slider", &cs.hsv[2], 0.0f, 1.0f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##HSV_V_input", &cs.hsv[2], 0.01f, 0.05f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();

            if (color_changed) {
                cs.hsv[0] = fmodf(cs.hsv[0], 360.0f); if (cs.hsv[0] < 0) cs.hsv[0] += 360.0f;
                cs.hsv[1] = std::clamp(cs.hsv[1], 0.0f, 1.0f);
                cs.hsv[2] = std::clamp(cs.hsv[2], 0.0f, 1.0f);
                hsv_to_rgb(cs.hsv[0], cs.hsv[1], cs.hsv[2], cs.rgb[0], cs.rgb[1], cs.rgb[2]);
                rgb_to_hsl(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsl[0], cs.hsl[1], cs.hsl[2]);
                rgb_to_hsi(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsi[0], cs.hsi[1], cs.hsi[2]);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("HSI")) {
            ImGui::Text("Teinte HSI (°) : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##HSI_H_slider", &cs.hsi[0], 0.0f, 360.0f, "%.1f°")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##HSI_H_input", &cs.hsi[0], 1.0f, 10.0f, "%.1f°")) color_changed = true;
            ImGui::PopItemWidth();

            ImGui::Text("Saturation HSI : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##HSI_S_slider", &cs.hsi[1], 0.0f, 1.0f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##HSI_S_input", &cs.hsi[1], 0.01f, 0.05f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();

            ImGui::Text("Intensité HSI : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##HSI_I_slider", &cs.hsi[2], 0.0f, 1.0f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##HSI_I_input", &cs.hsi[2], 0.01f, 0.05f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();

            if (color_changed) {
                cs.hsi[0] = fmodf(cs.hsi[0], 360.0f); if (cs.hsi[0] < 0) cs.hsi[0] += 360.0f;
                cs.hsi[1] = std::clamp(cs.hsi[1], 0.0f, 1.0f);
                cs.hsi[2] = std::clamp(cs.hsi[2], 0.0f, 1.0f);
                hsi_to_rgb(cs.hsi[0], cs.hsi[1], cs.hsi[2], cs.rgb[0], cs.rgb[1], cs.rgb[2]);
                rgb_to_hsl(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsl[0], cs.hsl[1], cs.hsl[2]);
                rgb_to_hsv(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsv[0], cs.hsv[1], cs.hsv[2]);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("CMYK & Blanc")) {
            const char* names[] = {"Cyan", "Magenta", "Jaune", "Noir"};
            for (int k=0; k<4; ++k) {
                ImGui::Text("%s : ", names[k]);
                ImGui::PushItemWidth(250.0f);
                if (ImGui::SliderFloat(("##CMYK_s_" + std::to_string(k)).c_str(), &cs.cmyk[k], 0.0f, 1.0f, "%.2f")) color_changed = true;
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::PushItemWidth(90.0f);
                if (ImGui::InputFloat(("##CMYK_i_" + std::to_string(k)).c_str(), &cs.cmyk[k], 0.01f, 0.05f, "%.2f")) color_changed = true;
                ImGui::PopItemWidth();
                cs.cmyk[k] = std::clamp(cs.cmyk[k], 0.0f, 1.0f);
            }

            ImGui::Text("Gain Blanc : ");
            ImGui::PushItemWidth(250.0f);
            if (ImGui::SliderFloat("##White_s", &cs.white, 0.0f, 2.0f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(90.0f);
            if (ImGui::InputFloat("##White_i", &cs.white, 0.05f, 0.1f, "%.2f")) color_changed = true;
            ImGui::PopItemWidth();
            cs.white = std::clamp(cs.white, 0.0f, 2.0f);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (color_changed) {
        for (int i=0;i<3;++i) g_config.rgb[i] = cs.rgb[i];
        for (int i=0;i<4;++i) g_config.cmyk[i] = cs.cmyk[i];
        g_config.white = cs.white;
        if (g_live_apply_all) {
            apply_color_adjust(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.cmyk[0], cs.cmyk[1], cs.cmyk[2], cs.white, cs.cmyk[3]);
        }
    }

    ImGui::Text("Aperçu couleur : ");
    ImGui::SameLine();
    ImGui::ColorButton("##ColorPreview", ImVec4(cs.rgb[0], cs.rgb[1], cs.rgb[2], 1.0f), 0, ImVec2(40, 20));
    ImGui::SameLine();
    ImGui::TextDisabled("RVB: (%.3f, %.3f, %.3f) | HSL: (%.0f°, %.0f%%, %.0f%%) | HSV: (%.0f°, %.0f%%, %.0f%%) | HSI: (%.0f°, %.0f%%, %.0f%%)",
        cs.rgb[0], cs.rgb[1], cs.rgb[2],
        cs.hsl[0], cs.hsl[1] * 100.0f, cs.hsl[2] * 100.0f,
        cs.hsv[0], cs.hsv[1] * 100.0f, cs.hsv[2] * 100.0f,
        cs.hsi[0], cs.hsi[1] * 100.0f, cs.hsi[2] * 100.0f);

    ImGui::Spacing();
    if (ImGui::Button("Appliquer Tous les Réglages d'Image")) {
        for (int i=0;i<3;++i) g_config.rgb[i] = cs.rgb[i];
        for (int i=0;i<4;++i) g_config.cmyk[i] = cs.cmyk[i];
        g_config.white = cs.white;
        g_config.brightness = brightness;
        g_config.contrast = contrast;
        apply_color_adjust(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.cmyk[0], cs.cmyk[1], cs.cmyk[2], cs.white, cs.cmyk[3]);
        apply_brightness(brightness);
        apply_contrast(contrast);
        apply_shadows_highlights(shadows, highlights);
    }
    ImGui::SameLine();
    if (ImGui::Button("🔄 Réinitialiser Couleurs & Image")) {
        brightness = 300;
        contrast = 1.0f;
        shadows = 0.0f;
        highlights = 1.0f;
        cs.rgb[0] = cs.rgb[1] = cs.rgb[2] = 1.0f;
        cs.cmyk[0] = cs.cmyk[1] = cs.cmyk[2] = cs.cmyk[3] = 0.0f;
        cs.white = 1.0f;
        rgb_to_hsl(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsl[0], cs.hsl[1], cs.hsl[2]);
        rgb_to_hsv(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsv[0], cs.hsv[1], cs.hsv[2]);
        rgb_to_hsi(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.hsi[0], cs.hsi[1], cs.hsi[2]);
        for (int i=0;i<3;++i) g_config.rgb[i] = cs.rgb[i];
        for (int i=0;i<4;++i) g_config.cmyk[i] = cs.cmyk[i];
        g_config.white = cs.white;
        g_config.brightness = brightness;
        g_config.contrast = contrast;
        apply_color_adjust(cs.rgb[0], cs.rgb[1], cs.rgb[2], cs.cmyk[0], cs.cmyk[1], cs.cmyk[2], cs.white, cs.cmyk[3]);
        apply_brightness(brightness);
        apply_contrast(contrast);
        apply_shadows_highlights(shadows, highlights);
    }
    ImGui::Separator();

    // 5. EDITEUR LUT
    edit_lut_curve();

    // 6. GAMMA & PARAMETRES SYSTEME
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "--- Gamma & Paramètres Système ---");

    static int gamma_idx = 3;
    const char* gamma_items[] = {"1.6","1.8","2.0","2.2","2.4","2.6"};
    ImGui::Combo("Gamma Preset", &gamma_idx, gamma_items, IM_ARRAYSIZE(gamma_items));
    if (ImGui::Button("Appliquer Gamma")) {
        float g = atof(gamma_items[gamma_idx]);
        g_config.gamma = g;
        apply_gamma(g);
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
    ImGui::Combo("Résolution", &res_idx, g_known_resolutions, IM_ARRAYSIZE(g_known_resolutions));
    if (ImGui::Button("Appliquer Résolution")) {
        int w,h;
        sscanf(g_known_resolutions[res_idx], "%dx%d", &w, &h);
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
    const char* color_modes[] = {
        "sRGB (Standard)",
        "DCI-P3 (Cinéma)",
        "Adobe RGB (1998) Wide Gamut",
        "Apple RGB (Mac Classic Gamma 1.8)",
        "Bruce RGB (Gamma 2.2 - D65)",
        "ProPhoto RGB / ROMM (Gamma 1.8 - D50)",
        "ColorMatch RGB (Gamma 1.8 - D50)",
        "NTSC RGB (Gamma 2.2 - White C)",
        "PAL/SECAM RGB (Gamma 2.2 - D65)",
        "Wide Gamut RGB (Gamma 2.2 - D50)"
    };
    ImGui::Combo("Mode couleur", &color_mode, color_modes, IM_ARRAYSIZE(color_modes));
    if (ImGui::Button("Appliquer Mode")) {
        g_config.color_mode = color_mode;
        apply_color_mode(color_mode);
    }

    static bool autostart = g_config.autostart;
    ImGui::Checkbox("Charger au démarrage du système", &autostart);
    if (ImGui::Button("Appliquer démarrage automatique")) {
        g_config.autostart = autostart;
        apply_autostart(autostart);
    }

    if (ImGui::Button("Sauvegarder la configuration")) {
        save_current_config();
    }

    ImGui::EndChild();

    if (!g_status_message.empty()) {
        ImGui::TextColored(g_status_ok ? ImVec4(0.4f, 0.85f, 0.4f, 1.0f) : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                            "%s", g_status_message.c_str());
    }
}