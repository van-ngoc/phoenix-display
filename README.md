# 🪶 Phœnix-display 2.0

**Phœnix-display** est une application professionnelle de contrôle d'affichage, d'étalonnage colorimétrique et de test d'écran pour les postes de travail Linux. Elle intègre une interface moderne écrite en C++ avec Dear ImGui et OpenGL, un pipeline de gestion de la couleur ACES, les courbes de tonalité style Adobe Lightroom Classic, des transformations d'adaptation chromatique (CAT) et 18 mires de tests vidéo 18-bit.

---

## 🚀 Fonctionnalités Principales

### 1. 🎛️ Contrôle Général & Étalonnage Écran
- **Contrôles Matériels XRandR / X11** : Luminance (lux), contraste, gamma relatif (1.0 à 3.0), gains RVB, sous-traitance CMYK, et canal Blanc.
- **Niveaux de Luminance Cible ($\text{cd/m}^2$)** :
  - **Niveau de blanc** : Ajustement du pic de luminance (ex: $120.00\text{ cd/m}^2$).
  - **Niveau de noir** : Ajustement du plancher de noir (ex: $0.000000\text{ cd/m}^2$).
- **Espaces de Couleur de Travail (Bruce Lindbloom)** : sRGB, DCI-P3, Adobe RGB (1998), Apple RGB (1.8), Bruce RGB (2.2 D65), ProPhoto RGB (ROMM), ColorMatch RGB, NTSC, PAL/SECAM, Wide Gamut RGB.

### 2. 🎨 Panneau Courbe des Tonalités (Style Lightroom Classic)
- **Mode Paramétrique** : Ombres, Tons sombres, Tons clairs, Hautes lumières avec barre interactive de séparation de zones à 3 curseurs.
- **Mode Courbe à Points** : Interpolation par spline cubique monotone (Fritsch-Carlson), sélecteur de canal (RVB, R, V, B), jauges d'entrée/sortie (`Entrée : 67 / Sortie : 37`), curseur *Affiner la saturation* et préconfigurations (*Linéaire, Contraste moyen, Contraste fort*).
- **Histogramme d'arrière-plan** dynamique intégré.

### 3. 🎬 Gestion de la Couleur ACES Display (DaVinci Resolve / NobeDisplay Style)
- **Color Science** : ACEScct v1.3, ACEScc v1.3, ACEScg (VFX), ACES2065-1, DaVinci YRGB Color Managed.
- **IDT (Transformée d'Entrée)** : 13 espaces de caméras et profils (Rec.709, ARRI Alexa LogC3, REDWideGamutRGB/REDlogFilm, Sony S-Gamut3.Cine/S-Log3, etc.).
- **ODT (Transformée de Sortie Écran)** : Rec.709 (100 nits), sRGB, DCI-P3, Rec.2020 PQ HDR (1000 nits), Rec.2020 HLG HDR.
- **6 Régleurs ACES RAW Controls** :
  - **Color Temp** : 2000K à 12000K (Kelvin)
  - **Tint** : -50.0 à +50.0 (Green / Magenta)
  - **DRX** : 0.00 à 1.00 (Dynamic Range Extension / Knee Highlight Recovery)
  - **Shadow** : -1.00 à +1.00 (Shadow Lift)
  - **Exposure** : -5.00 à +5.00 EV
  - **Brightness** : -1.00 à +1.00
- **Visualiseur de Courbe S ACES RRT/ODT** interactif.

### 4. 💡 Adaptation Chromatique (CAT - Chromatic Adaptation Transform)
- **Transformations CAT** : Bradford (Standard ICC & ACES), CAT02 (CIE CAM02), Von Kries, XYZ Scaling, Sharp CAT.
- **Illuminants de Référence** : D65 (6504K), D50 (5003K), D55 (5500K), Illuminant A (2856K), Illuminant C (6774K), Illuminant E (5000K).

### 5. 🏁 18 Mires de Test Vidéo et d'Étalonnage
1. **SMPTE / EBU Color Bars** (Mire d'alignement vidéo standard)
2. **ARIB STD-B28 HD Bars** (Mire HD de référence broadcast)
3. **Plaque de Zone de Fresnel** (Test d'Aliasing / Moiré)
4. **Charte MacBeth 24 ColorChecker** (Fidélité des teintes chairs et neutres)
5. **Convergence Sous-pixellique RGB** (Alignement physique des sous-pixels)
6. **Échelle Gamma Visuelle** (Calibration visuelle directe Gamma 1.8 à 2.6)
7. **Dynamic Range Starburst HDR** (Test d'éblouissement et blooming)
8. **Matrice HSL 2D** (Distribution des nuances de teinte et saturation)
9. **Netteté 1px / 2px** (Filtres d'accentuation et aliasing)
10. **Grille de Cadrage 16:9 / 4:3** (Safe Area 90% / 80%)
11. **Roue Chromatique HSL 360°** (Continuité des couleurs)
12. **Dégradés Continu RVB & Gris** (Vérification du banding 8/10-bit)
13. **PLUGE (0% - 8%)** (Réglage précis du niveau de noir)
14. **Clipping Hautes Lumières (92% - 100%)** (Réglage précis des blancs)
15. **Uniformité de Dalle** (Recherche de pixels morts R, G, B, Blanc, Noir)
16. **Damier ANSI** (Contraste simultané et fuites de lumière)
17. **Test de Rémanence / Lag** (Temps de réponse et motion blur)
18. **Échelle de Gris 16 Niveaux** (Linéarité des échelons de gris)

### 6. 🔄 Harmonisation des Réinitialisations
- Bouton Maître **`[ 🔄 Réinitialiser TOUS les Réglages d'Affichage (Défaut Usine) ]`** : Remise à zéro instantanée à 100% de clarté lumineuse par défaut.
- Boutons de réinitialisation dédiés `🔄` dans chaque module.

---

## 🛠️ Installation & Lancement

Consultez [install.md](install.md) pour les détails.

```bash
# Clone du dépôt
git clone https://github.com/van-ngoc/phoenix-display
cd phoenix-display

# Installation automatique des dépendances et déploiement
./install.sh

# Lancement

./PhoenixDisplay
```

### 🖥️ Raccourci Bureau & Menu Système
Le script d'installation déploie automatiquement le lanceur avec son icône HD 512x512 dans :
- Menu des applications : `~/.local/share/applications/phoenix-display.desktop`
- Bureau : `~/Desktop/phoenix-display.desktop` et `~/Bureau/phoenix-display.desktop`

---

## 📄 Licence
Développé pour Linux / X11 avec C++, OpenGL et Dear ImGui.
