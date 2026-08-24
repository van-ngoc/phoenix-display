#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "gui.h"
#include "display_controls.h"
#include "test_patterns.h"
#include <iostream>

static bool g_fullscreen = false;
static int g_windowed_x = 100, g_windowed_y = 100;
static int g_windowed_w = 1280, g_windowed_h = 800;

static void toggle_fullscreen(GLFWwindow* window) {
    g_fullscreen = !g_fullscreen;
    if (g_fullscreen) {
        glfwGetWindowPos(window, &g_windowed_x, &g_windowed_y);
        glfwGetWindowSize(window, &g_windowed_w, &g_windowed_h);
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (monitor && mode) {
            glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
    } else {
        glfwSetWindowMonitor(window, nullptr, g_windowed_x, g_windowed_y, g_windowed_w, g_windowed_h, 0);
    }
}

int main() {
    if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE); // Fenêtre maximisée d'emblée

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Phœnix-display", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    init_display_controls();
    init_test_patterns();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
            toggle_fullscreen(window);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        GUI::render();

        if (GUI::consume_minimize_request()) {
            glfwIconifyWindow(window);
        }
        if (GUI::consume_fullscreen_request()) {
            toggle_fullscreen(window);
        }

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
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
