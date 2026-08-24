#include "gui.h"
#include "display_controls.h"
#include "test_patterns.h"
#include "utils.h"
#include <imgui.h>

static int current_tab = 0;
const char* tab_names[] = { "Réglages", "Tests", "Infos" };
static bool g_minimize_requested = false;
static bool g_fullscreen_requested = false;

bool GUI::consume_minimize_request() {
    bool v = g_minimize_requested;
    g_minimize_requested = false;
    return v;
}

bool GUI::consume_fullscreen_request() {
    bool v = g_fullscreen_requested;
    g_fullscreen_requested = false;
    return v;
}

void GUI::render() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGui::Begin("Phœnix-display", nullptr, 
        ImGuiWindowFlags_NoDecoration | 
        ImGuiWindowFlags_NoMove | 
        ImGuiWindowFlags_NoResize | 
        ImGuiWindowFlags_NoSavedSettings);

    if (ImGui::Button("Plein Écran (F11)")) {
        g_fullscreen_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Travailler en arrière-plan")) {
        g_minimize_requested = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(minimise la fenêtre ; les réglages restent actifs)");

    if (ImGui::BeginTabBar("MainTabs")) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::BeginTabItem(tab_names[i])) {
                current_tab = i;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    switch (current_tab) {
        case 0: render_settings(); break;
        case 1: render_tests(); break;
        case 2:
            ImGui::Text("Phœnix-display v1.0");
            ImGui::Text("Build pour Debian 13 - GTX 970");
            ImGui::Text("RAM: 32 Go | eGPU RTX 5060 (8 Go)");
            ImGui::Text("Sortie écran détectée: %s", get_output_name().c_str());
            ImGui::Text("Configuration chargée depuis phoenix_config.json");
            break;
    }

    ImGui::End();
}
