#include "utils.h"
#include <cstdio>
#include <memory>
#include <array>
#include <fstream>

#include <cmath>
#include <algorithm>

static constexpr float M_PI_F = 3.14159265358979323846f;

struct PipeDeleter {
    void operator()(FILE* f) const { if (f) pclose(f); }
};

std::string exec_cmd(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, PipeDeleter> pipe(popen(cmd, "r"));
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

void rgb_to_hsl(float r, float g, float b, float& h, float& s, float& l) {
    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);
    float max_v = std::max({r, g, b});
    float min_v = std::min({r, g, b});
    float delta = max_v - min_v;

    l = (max_v + min_v) / 2.0f;

    if (delta < 1e-6f) {
        h = 0.0f;
        s = 0.0f;
    } else {
        s = (l > 0.5f) ? (delta / (2.0f - max_v - min_v)) : (delta / (max_v + min_v));
        if (max_v == r) {
            h = (g - b) / delta + (g < b ? 6.0f : 0.0f);
        } else if (max_v == g) {
            h = (b - r) / delta + 2.0f;
        } else {
            h = (r - g) / delta + 4.0f;
        }
        h *= 60.0f;
    }
}

static float hue_to_rgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

void hsl_to_rgb(float h, float s, float l, float& r, float& g, float& b) {
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    l = std::clamp(l, 0.0f, 1.0f);

    if (s < 1e-6f) {
        r = g = b = l;
    } else {
        float q = (l < 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
        float p = 2.0f * l - q;
        float hk = h / 360.0f;
        r = hue_to_rgb(p, q, hk + 1.0f / 3.0f);
        g = hue_to_rgb(p, q, hk);
        b = hue_to_rgb(p, q, hk - 1.0f / 3.0f);
    }
}

void rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v) {
    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);
    float max_v = std::max({r, g, b});
    float min_v = std::min({r, g, b});
    float delta = max_v - min_v;

    v = max_v;
    s = (max_v > 1e-6f) ? (delta / max_v) : 0.0f;

    if (delta < 1e-6f) {
        h = 0.0f;
    } else {
        if (max_v == r) {
            h = (g - b) / delta + (g < b ? 6.0f : 0.0f);
        } else if (max_v == g) {
            h = (b - r) / delta + 2.0f;
        } else {
            h = (r - g) / delta + 4.0f;
        }
        h *= 60.0f;
    }
}

void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b) {
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    if (s < 1e-6f) {
        r = g = b = v;
        return;
    }

    float hh = h / 60.0f;
    int i = (int)hh;
    float ff = hh - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - (s * ff));
    float t = v * (1.0f - (s * (1.0f - ff)));

    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: default: r = v; g = p; b = q; break;
    }
}

void rgb_to_hsi(float r, float g, float b, float& h, float& s, float& i) {
    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);

    i = (r + g + b) / 3.0f;
    float min_v = std::min({r, g, b});

    if (i < 1e-6f) {
        s = 0.0f;
        h = 0.0f;
        i = 0.0f;
        return;
    }

    s = 1.0f - (min_v / i);

    float num = 0.5f * ((r - g) + (r - b));
    float den = sqrtf((r - g) * (r - g) + (r - b) * (g - b)) + 1e-6f;
    float theta = acosf(std::clamp(num / den, -1.0f, 1.0f)) * (180.0f / M_PI_F);

    h = (b > g) ? (360.0f - theta) : theta;
}

void hsi_to_rgb(float h, float s, float i, float& r, float& g, float& b) {
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    i = std::clamp(i, 0.0f, 1.0f);

    if (s < 1e-6f) {
        r = g = b = i;
        return;
    }

    float rad = h * (M_PI_F / 180.0f);

    if (h < 120.0f) {
        b = i * (1.0f - s);
        r = i * (1.0f + (s * cosf(rad)) / cosf(M_PI_F / 3.0f - rad));
        g = 3.0f * i - (r + b);
    } else if (h < 240.0f) {
        rad = (h - 120.0f) * (M_PI_F / 180.0f);
        r = i * (1.0f - s);
        g = i * (1.0f + (s * cosf(rad)) / cosf(M_PI_F / 3.0f - rad));
        b = 3.0f * i - (r + g);
    } else {
        rad = (h - 240.0f) * (M_PI_F / 180.0f);
        g = i * (1.0f - s);
        b = i * (1.0f + (s * cosf(rad)) / cosf(M_PI_F / 3.0f - rad));
        r = 3.0f * i - (g + b);
    }

    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);
}

