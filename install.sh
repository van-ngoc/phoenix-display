#!/bin/bash
set -e

echo "=== Installation de Phœnix-display ==="

sudo apt update

echo "Installation des dépendances principales..."
sudo apt install -y \
    build-essential \
    cmake \
    libglfw3-dev \
    libgl1-mesa-dev \
    libx11-dev \
    libxrandr-dev \
    libxcb1-dev \
    libjsoncpp-dev \
    pkg-config \
    git

echo "Tentative d'installation de libgps-dev (optionnel)..."
sudo apt install -y libgps-dev || echo "libgps-dev non disponible, GPS désactivé."

if [ ! -d "imgui" ]; then
    echo "Téléchargement de Dear ImGui..."
    git clone https://github.com/ocornut/imgui.git
else
    echo "ImGui déjà présent."
fi

# Si le dossier imgui/backends n'existe pas (ancienne version), on le crée
if [ ! -d "imgui/backends" ] && [ -d "imgui/examples" ]; then
    echo "Restructuration des backends (ancienne version d'ImGui)..."
    mkdir -p imgui/backends
    cp -r imgui/examples/imgui_impl_glfw.* imgui/backends/
    cp -r imgui/examples/imgui_impl_opengl3.* imgui/backends/
fi

mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ..

echo "Installation de l'exécutable dans /usr/local/bin/phoenix-display..."
sudo install -m 755 build/PhoenixDisplay /usr/local/bin/phoenix-display

echo "Installation de l'icône de l'application..."
sudo install -d /usr/share/pixmaps
sudo install -m 644 phoenix-display.png /usr/share/pixmaps/phoenix-display.png
sudo install -d /usr/share/icons/hicolor/512x512/apps
sudo install -m 644 phoenix-display.png /usr/share/icons/hicolor/512x512/apps/phoenix-display.png

echo "Installation du raccourci dans le menu des applications..."
sudo install -m 644 phoenix-display.desktop /usr/share/applications/phoenix-display.desktop

USER_HOME=$(eval echo "~${SUDO_USER:-$USER}")
USER_NAME="${SUDO_USER:-$USER}"

echo "Installation dans ~/.local/bin et ~/.local/share/applications..."
mkdir -p "$USER_HOME/.local/bin" "$USER_HOME/.local/share/applications" "$USER_HOME/.local/share/pixmaps"
cp build/PhoenixDisplay "$USER_HOME/.local/bin/phoenix-display"
chmod +x "$USER_HOME/.local/bin/phoenix-display"
cp phoenix-display.desktop "$USER_HOME/.local/share/applications/phoenix-display.desktop"
chmod +x "$USER_HOME/.local/share/applications/phoenix-display.desktop"
cp phoenix-display.png "$USER_HOME/.local/share/pixmaps/phoenix-display.png"
chown -R $USER_NAME:$USER_NAME "$USER_HOME/.local/bin/phoenix-display" "$USER_HOME/.local/share/applications/phoenix-display.desktop" "$USER_HOME/.local/share/pixmaps/phoenix-display.png" 2>/dev/null || true
gio trust "$USER_HOME/.local/share/applications/phoenix-display.desktop" 2>/dev/null || true
update-desktop-database "$USER_HOME/.local/share/applications" 2>/dev/null || true

for DESK_DIR in "$USER_HOME/Desktop" "$USER_HOME/Bureau"; do
    if [ -d "$DESK_DIR" ]; then
        echo "Installation du raccourci sur le Bureau ($DESK_DIR)..."
        cp phoenix-display.desktop "$DESK_DIR/phoenix-display.desktop"
        chmod +x "$DESK_DIR/phoenix-display.desktop"
        chown $USER_NAME:$USER_NAME "$DESK_DIR/phoenix-display.desktop" 2>/dev/null || true
        gio trust "$DESK_DIR/phoenix-display.desktop" 2>/dev/null || true
    fi
done

echo "=== Installation terminée ==="
echo "Lancez l'application avec la commande 'phoenix-display' ou depuis le raccourci sur le bureau."