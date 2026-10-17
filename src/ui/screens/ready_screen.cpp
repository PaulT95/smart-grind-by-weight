#include "ready_screen.h"
#include <Arduino.h>
#include "../../config/constants.h"
#include "../../controllers/grind_mode_traits.h"
#include "../event_bridge_lvgl.h"
#ifndef SMART_GRIND_SIM
#include "../../network/network_manager.h"
#include "../../network/provisioning_service.h"
#endif
#include "../ui_helpers.h"

namespace {

bool ready_text_empty(const ReadyScreenText& value) {
#ifdef SMART_GRIND_SIM
    return value.empty();
#else
    return value.isEmpty();
#endif
}

// Long enough to show several frames on hardware while remaining much more
// responsive than LVGL's 200-400 ms default scroll timing.
constexpr uint32_t READY_TAB_SWIPE_ANIMATION_MS = 180;

void shorten_tab_scroll_animation(lv_event_t* event) {
    lv_anim_t* animation = lv_event_get_scroll_anim(event);
    if (animation) {
        lv_anim_set_duration(animation, READY_TAB_SWIPE_ANIMATION_MS);
    }
}

#ifndef SMART_GRIND_SIM
String escape_wifi_qr_value(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char ch = value[i];
        if (ch == '\\' || ch == ';' || ch == ',' || ch == ':' || ch == '"') escaped += '\\';
        escaped += ch;
    }
    return escaped;
}
#endif

} // namespace

void ReadyScreen::create() {
    screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(screen, LV_PCT(100), LV_PCT(80));
    lv_obj_align(screen, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Create tabview
    tabview = lv_tabview_create(screen);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_align(tabview, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(tabview, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_flag(tabview, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // LVGL defaults scroll snapping to 200-400 ms. On the full-width AMOLED
    // tabs that makes a completed swipe feel noticeably behind the finger.
    lv_obj_t* tab_content = lv_tabview_get_content(tabview);
    lv_obj_add_event_cb(tab_content, shorten_tab_scroll_animation, LV_EVENT_SCROLL_BEGIN, nullptr);

    // Hide tab buttons for swipe-only interface
    lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_HIDDEN);

    // Transparent background
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, 0);

    // Add manual and profile tabs
    manual_tab = lv_tabview_add_tab(tabview, "Manual");
    profile_tabs[0] = lv_tabview_add_tab(tabview, "Single");
    profile_tabs[1] = lv_tabview_add_tab(tabview, "Double");
    profile_tabs[2] = lv_tabview_add_tab(tabview, "Custom");
    wifi_tab = lv_tabview_add_tab(tabview, "WI-FI");
    menu_tab = lv_tabview_add_tab(tabview, "MENU");
    knob_tab = lv_tabview_add_tab(tabview, "KNOB");

    create_manual_page(manual_tab);

    // Default weights
    float default_weights[3] = {USER_SINGLE_ESPRESSO_WEIGHT_G, USER_DOUBLE_ESPRESSO_WEIGHT_G, USER_CUSTOM_PROFILE_WEIGHT_G};
    const char* names[3] = {"SINGLE", "DOUBLE", "CUSTOM"};
    
    for (int i = 0; i < 3; i++) {
        create_profile_page(profile_tabs[i], i, names[i], default_weights[i]);
    }

    create_wifi_page(wifi_tab);

    // Create menu tab page
    create_menu_page(menu_tab);

    // Create DJ-style knob dial tab page
    create_knob_page(knob_tab);

    update_profile_values(default_weights, GrindMode::WEIGHT);

    visible = false;
}

void ReadyScreen::create_manual_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 4, 0);
    lv_obj_set_style_pad_gap(parent, 7, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "MANUAL");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    manual_weight_label = lv_label_create(parent);
    lv_label_set_text(manual_weight_label, "0.0g");
    lv_obj_set_width(manual_weight_label, LV_PCT(100));
    lv_obj_set_style_text_font(manual_weight_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(manual_weight_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(manual_weight_label, LV_TEXT_ALIGN_CENTER, 0);

    manual_scale_status_label = lv_label_create(parent);
    lv_label_set_text(manual_scale_status_label, "LIVE WEIGHT");
    lv_obj_set_style_text_font(manual_scale_status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(manual_scale_status_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(manual_scale_status_label, LV_TEXT_ALIGN_CENTER, 0);

    manual_tare_button = create_button(parent, "TARE", lv_color_hex(THEME_COLOR_PRIMARY),
                                       230, 68, &lv_font_montserrat_28);

    lv_obj_t* safety_label = lv_label_create(parent);
    lv_label_set_text(safety_label, "30 SECOND LIMIT");
    lv_obj_set_style_text_font(safety_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(safety_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(safety_label, LV_TEXT_ALIGN_CENTER, 0);
}

void ReadyScreen::update_manual_scale(float weight, bool available, bool taring) {
    if (!manual_weight_label || !manual_scale_status_label || !manual_tare_button) return;

    if (!available) {
        lv_label_set_text(manual_weight_label, "--");
        lv_label_set_text(manual_scale_status_label, "SCALE UNAVAILABLE");
        lv_obj_add_state(manual_tare_button, LV_STATE_DISABLED);
        return;
    }

    if (taring) {
        lv_label_set_text(manual_weight_label, "TARING");
        lv_label_set_text(manual_scale_status_label, "KEEP SCALE CLEAR");
        lv_obj_add_state(manual_tare_button, LV_STATE_DISABLED);
        return;
    }

    char weight_text[16];
    snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(manual_weight_label, weight_text);
    lv_label_set_text(manual_scale_status_label, "LIVE WEIGHT");
    lv_obj_clear_state(manual_tare_button, LV_STATE_DISABLED);
}

void ReadyScreen::create_wifi_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 4, 0);
    lv_obj_set_style_pad_gap(parent, 5, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    wifi_status_label = lv_label_create(parent);
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(wifi_status_label, LV_TEXT_ALIGN_CENTER, 0);

    wifi_qr = lv_qrcode_create(parent);
    lv_qrcode_set_size(wifi_qr, 170);
    lv_qrcode_set_dark_color(wifi_qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(wifi_qr, lv_color_hex(0xFFFFFF));
    lv_qrcode_set_quiet_zone(wifi_qr, true);

    wifi_detail_label = lv_label_create(parent);
    lv_obj_set_width(wifi_detail_label, LV_PCT(94));
    lv_label_set_long_mode(wifi_detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(wifi_detail_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_detail_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_align(wifi_detail_label, LV_TEXT_ALIGN_CENTER, 0);

    update_network_status();
}

void ReadyScreen::update_network_status() {
    if (!wifi_status_label || !wifi_detail_label || !wifi_qr) return;

    ReadyScreenText status;
    ReadyScreenText detail;
    ReadyScreenText qr_payload;
#ifdef SMART_GRIND_SIM
    status = "WI-FI SIMULATED";
    detail = "smartgrind.local\n192.168.50.160";
#else
    switch (network_manager.state()) {
        case NetworkState::WIFI_DISABLED:
            status = "WI-FI DISABLED";
            detail = "Enable Wi-Fi in Settings.";
            break;
        case NetworkState::WIFI_NO_CREDENTIALS:
        case NetworkState::WIFI_SETUP_REQUIRED:
            status = "STARTING WI-FI SETUP";
            detail = "Preparing the setup network...";
            break;
        case NetworkState::WIFI_CONNECTING:
            status = "CONNECTING";
            detail = network_manager.network_name();
            break;
        case NetworkState::WIFI_CONNECTED:
            status = "WI-FI CONNECTED";
            detail = network_manager.network_name() + "\nhttp://" + network_manager.hostname() +
                     ".local\n" + network_manager.ip_address();
            break;
        case NetworkState::WIFI_RETRY_WAIT:
            status = "WI-FI INTERRUPTED";
            detail = "Reconnecting automatically...";
            break;
        case NetworkState::WIFI_SETUP_AP: {
            status = "CONNECT WI-FI";
            const String& ssid = provisioning_service.access_point_ssid();
            const String& password = provisioning_service.access_point_password();
            detail = "Scan with your phone, then use the sign-in page.\n" + ssid;
            qr_payload = "WIFI:T:";
            qr_payload += password.isEmpty() ? "nopass" : "WPA";
            qr_payload += ";S:" + escape_wifi_qr_value(ssid);
            if (!password.isEmpty()) qr_payload += ";P:" + escape_wifi_qr_value(password);
            qr_payload += ";;";
            break;
        }
    }
#endif

    const ReadyScreenText& rendered_status = status;
    const ReadyScreenText& rendered_detail = detail;
    const ReadyScreenText& rendered_qr = qr_payload;
    if (rendered_status != wifi_status_text) {
        lv_label_set_text(wifi_status_label, rendered_status.c_str());
        wifi_status_text = rendered_status;
    }
    if (rendered_detail != wifi_detail_text) {
        lv_label_set_text(wifi_detail_label, rendered_detail.c_str());
        wifi_detail_text = rendered_detail;
    }
    if (ready_text_empty(rendered_qr)) {
        lv_obj_add_flag(wifi_qr, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (rendered_qr != wifi_qr_payload) {
            lv_qrcode_update(wifi_qr, rendered_qr.c_str(), static_cast<uint32_t>(rendered_qr.length()));
            wifi_qr_payload = rendered_qr;
        }
        lv_obj_clear_flag(wifi_qr, LV_OBJ_FLAG_HIDDEN);
    }
}

void ReadyScreen::create_profile_page(lv_obj_t* parent, int profile_index, const char* profile_name, float weight) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 0, 0);

    lv_obj_t* name_label;
    (void)create_profile_label(parent, &name_label, &weight_labels[profile_index]);
    lv_label_set_text(name_label, profile_name);
    lv_obj_add_flag(name_label, LV_OBJ_FLAG_CLICKABLE);
    
    char weight_text[16];
    snprintf(weight_text, sizeof(weight_text), SYS_WEIGHT_DISPLAY_FORMAT, weight);
    lv_label_set_text(weight_labels[profile_index], weight_text);
    lv_obj_add_flag(weight_labels[profile_index], LV_OBJ_FLAG_CLICKABLE);
}

void ReadyScreen::create_menu_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(parent, 20, 0);

    // Info label
    lv_obj_t* info_label = lv_label_create(parent);
    lv_label_set_text(info_label, "MAIN\nMENU");
    lv_obj_set_style_text_font(info_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(info_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
}

void ReadyScreen::show() {
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = true;
}

void ReadyScreen::hide() {
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
    visible = false;
}

void ReadyScreen::update_profile_values(const float values[3], GrindMode mode) {
    for (int i = 0; i < 3; i++) {
        if (weight_labels[i]) {
            char text[24];
            format_ready_value(text, sizeof(text), mode, values[i]);
            lv_label_set_text(weight_labels[i], text);
        }
    }
}

void ReadyScreen::set_active_tab(int tab) {
    if (tab >= 0 && tab < TAB_COUNT) {
        lv_tabview_set_act(tabview, tab, LV_ANIM_OFF);
    }
}

void ReadyScreen::set_profile_long_press_handler(lv_event_cb_t handler) {
    for (int i = 0; i < 3; i++) {
        if (weight_labels[i]) {
            lv_obj_add_event_cb(weight_labels[i], handler, LV_EVENT_LONG_PRESSED, NULL);
        }
    }
}

void ReadyScreen::set_manual_tare_handler(lv_event_cb_t handler) {
    if (manual_tare_button && handler) {
        lv_obj_add_event_cb(manual_tare_button, handler, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(
                                EventBridgeLVGL::EventType::MANUAL_TARE)));
    }
}

// I changed from here on
#include <cmath>
#include <cstdio>

namespace {
uint32_t get_dial_theme_color(float dial) {
    if (std::isnan(dial)) return 0x555555;
    if (dial <= 1.0f) return 0x00E5FF;       // Cyan - Turkish / Zero
    if (dial <= 2.0f) return 0x2979FF;       // Royal Blue - Fine Espresso
    if (dial <= 3.0f) return 0x00E676;       // Emerald Green - Espresso / Moka
    if (dial <= 4.0f) return 0xFFD600;       // Warm Gold - Aeropress
    if (dial <= 4.8f) return 0xFF9100;       // Orange - Pour Over / Filter
    return 0xFF1744;                         // Deep Red - French Press / Coarse
}

const char* get_dial_grind_type(float dial) {
    if (std::isnan(dial)) return "UNCALIBRATED";
    if (dial < 0.3f) return "TOUCH / ZERO";
    if (dial <= 1.2f) return "TURKISH";
    if (dial <= 2.2f) return "ESPRESSO";
    if (dial <= 3.2f) return "MOKA POT";
    if (dial <= 4.2f) return "POUR OVER";
    return "FRENCH PRESS";
}
} // namespace

void ReadyScreen::create_knob_page(lv_obj_t* parent) {
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 4, 0);
    lv_obj_set_style_pad_gap(parent, 8, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // Title label
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "GRIND DIAL");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    // Circular DJ-style dial knob arc
    knob_arc = lv_arc_create(parent);
    lv_obj_set_size(knob_arc, 190, 190);
    lv_arc_set_rotation(knob_arc, 135);
    lv_arc_set_bg_angles(knob_arc, 0, 270);
    lv_arc_set_range(knob_arc, 0, 50); // 0.0 to 5.0 (0.1 resolution)
    lv_arc_set_value(knob_arc, 0);
    lv_obj_set_style_arc_color(knob_arc, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_width(knob_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(knob_arc, lv_color_hex(0x00E5FF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(knob_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(knob_arc, true, LV_PART_INDICATOR);
    lv_obj_remove_style(knob_arc, nullptr, LV_PART_KNOB); // Clean modern dial indicator
    lv_obj_clear_flag(knob_arc, LV_OBJ_FLAG_CLICKABLE);

    // Inside the arc: center container with dial value and raw ADC
    lv_obj_t* center_cont = lv_obj_create(knob_arc);
    lv_obj_set_size(center_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(center_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(center_cont, 0, 0);
    lv_obj_set_style_pad_all(center_cont, 0, 0);
    lv_obj_set_layout(center_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(center_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(center_cont);
    lv_obj_clear_flag(center_cont, LV_OBJ_FLAG_SCROLLABLE);

    knob_dial_label = lv_label_create(center_cont);
    lv_label_set_text(knob_dial_label, "--");
    lv_obj_set_style_text_font(knob_dial_label, &lv_font_montserrat_56, 0);
    lv_obj_set_style_text_color(knob_dial_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);

    // Inside the arc, under the number: raw ADC value ("raw:1234")
    knob_badge_label = lv_label_create(center_cont);
    lv_label_set_text(knob_badge_label, "raw:--");
    lv_obj_set_style_text_font(knob_badge_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(knob_badge_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);

    // Below the arc: grind type, colored to match the dial
    knob_adc_label = lv_label_create(parent);
    lv_label_set_text(knob_adc_label, "UNCALIBRATED");
    lv_obj_set_style_text_font(knob_adc_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(knob_adc_label, lv_color_hex(THEME_COLOR_WARNING), 0);
}

void ReadyScreen::update_grind_knob(float dial, int32_t raw_adc, bool is_calibrated) {
    const bool valid = is_calibrated && !std::isnan(dial);

    // Arc
    if (knob_arc) {
        if (!valid) {
            lv_arc_set_value(knob_arc, 0);
            lv_obj_set_style_arc_color(knob_arc, lv_color_hex(0x555555), LV_PART_INDICATOR);
        } else {
            int32_t arc_val = static_cast<int32_t>(dial * 10.0f + 0.5f);
            if (arc_val < 0) arc_val = 0;
            if (arc_val > 50) arc_val = 50;
            lv_arc_set_value(knob_arc, arc_val);
            lv_obj_set_style_arc_color(knob_arc, lv_color_hex(get_dial_theme_color(dial)), LV_PART_INDICATOR);
        }
    }

    // Big dial number
    if (knob_dial_label) {
        if (!valid) {
            lv_label_set_text(knob_dial_label, "--");
            lv_obj_set_style_text_color(knob_dial_label, lv_color_hex(THEME_COLOR_TEXT_SECONDARY), 0);
        } else {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.1f", dial);
            lv_label_set_text(knob_dial_label, buf);
            lv_obj_set_style_text_color(knob_dial_label, lv_color_hex(THEME_COLOR_TEXT_PRIMARY), 0);
        }
    }

    // Inside the arc: raw ADC value
    if (knob_badge_label) {
        char buf[24];
        snprintf(buf, sizeof(buf), "raw:%ld", static_cast<long>(raw_adc));
        lv_label_set_text(knob_badge_label, buf);
    }

    // Below the arc: grind type
    if (knob_adc_label) {
        if (!valid) {
            lv_label_set_text(knob_adc_label, "UNCALIBRATED");
            lv_obj_set_style_text_color(knob_adc_label, lv_color_hex(THEME_COLOR_WARNING), 0);
        } else {
            lv_label_set_text(knob_adc_label, get_dial_grind_type(dial));
            lv_obj_set_style_text_color(knob_adc_label, lv_color_hex(get_dial_theme_color(dial)), 0);
        }
    }
}