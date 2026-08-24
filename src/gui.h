#pragma once
namespace GUI {
    void render();
    // Renvoie true une seule fois après un clic sur "Travailler en arrière-plan"
    bool consume_minimize_request();
    // Renvoie true une seule fois après un clic sur "Plein Écran"
    bool consume_fullscreen_request();
}
