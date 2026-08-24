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
    root["autostart"] = cfg.autostart;

    root["aces_enabled"] = cfg.aces_enabled;
    root["aces_color_science"] = cfg.aces_color_science;
    root["aces_input_space"] = cfg.aces_input_space;
    root["aces_output_transform"] = cfg.aces_output_transform;
    root["aces_exposure"] = cfg.aces_exposure;
    root["aces_color_temp"] = cfg.aces_color_temp;
    root["aces_tint"] = cfg.aces_tint;
    root["aces_drx"] = cfg.aces_drx;
    root["aces_shadow"] = cfg.aces_shadow;
    root["aces_brightness"] = cfg.aces_brightness;

    root["cat_method"] = cfg.cat_method;
    root["cat_source_illuminant"] = cfg.cat_source_illuminant;
    root["cat_target_illuminant"] = cfg.cat_target_illuminant;

    root["white_level_cdm2"] = cfg.white_level_cdm2;
    root["black_level_cdm2"] = cfg.black_level_cdm2;

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
    cfg.autostart = root.get("autostart", false).asBool();

    cfg.aces_enabled = root.get("aces_enabled", false).asBool();
    cfg.aces_color_science = root.get("aces_color_science", 0).asInt();
    cfg.aces_input_space = root.get("aces_input_space", 0).asInt();
    cfg.aces_output_transform = root.get("aces_output_transform", 0).asInt();
    cfg.aces_exposure = root.get("aces_exposure", 0.0f).asFloat();
    cfg.aces_color_temp = root.get("aces_color_temp", 6500.0f).asFloat();
    cfg.aces_tint = root.get("aces_tint", 0.0f).asFloat();
    cfg.aces_drx = root.get("aces_drx", 0.0f).asFloat();
    cfg.aces_shadow = root.get("aces_shadow", 0.0f).asFloat();
    cfg.aces_brightness = root.get("aces_brightness", 0.0f).asFloat();

    cfg.cat_method = root.get("cat_method", 0).asInt();
    cfg.cat_source_illuminant = root.get("cat_source_illuminant", 0).asInt();
    cfg.cat_target_illuminant = root.get("cat_target_illuminant", 0).asInt();

    cfg.white_level_cdm2 = root.get("white_level_cdm2", 120.0f).asFloat();
    cfg.black_level_cdm2 = root.get("black_level_cdm2", 0.0f).asFloat();

    return cfg;
}
