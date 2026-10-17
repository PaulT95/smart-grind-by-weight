#include "HallSensor.h"
#include <Arduino.h>
#include <cmath>
#include <vector>
#include <algorithm>

HallSensor::HallSensor()
    : pt_valid_(0),
      last_raw_adc_(0),
      last_dial_(NAN),
      filtered_adc_(0.0f),
      has_filtered_adc_(false),
      initialized_(false) {
    for (int i = 0; i < HW_HALL_NUM_CAL_POINTS; i++) {
        pt_adc_[i] = 0;
        pt_step_[i] = static_cast<float>(i); //we start at 0 because 0 is when the blade "touches"
    }
}

HallSensor::~HallSensor() = default;

void HallSensor::init(Preferences* /* preferences */) {
    pinMode(HW_HALL_ADC_PIN, INPUT);
    load_calibration();
    initialized_ = true;

    LOG_BLE("[HallSensor] Initialized on ADC pin %d (calibrated points: %d/6)\n",
            HW_HALL_ADC_PIN, get_calibrated_point_count());
}

int32_t HallSensor::read_raw_adc() {
    uint32_t sum = 0;
    for (int i = 0; i < HW_HALL_NUM_SAMPLES; i++) {
        sum += analogRead(HW_HALL_ADC_PIN);
        if (i < HW_HALL_NUM_SAMPLES - 1) {
            delay(1);
        }
    }

    int32_t sample = static_cast<int32_t>(sum / HW_HALL_NUM_SAMPLES);
    if (has_filtered_adc_) {
        // Exponential moving average filter to ensure rock-solid stability
        filtered_adc_ = (filtered_adc_ * 0.7f) + (static_cast<float>(sample) * 0.3f);
    } else {
        filtered_adc_ = static_cast<float>(sample);
        has_filtered_adc_ = true;
    }

    last_raw_adc_ = static_cast<int32_t>(filtered_adc_ + 0.5f);
    last_dial_ = calculate_dial(last_raw_adc_);
    return last_raw_adc_;
}

float HallSensor::calculate_dial(int32_t raw_adc) const {
    struct CalPoint {
        int32_t adc;
        float dial;
    };

    std::vector<CalPoint> valid_points;
    valid_points.reserve(HW_HALL_NUM_CAL_POINTS);

    for (int i = 0; i < HW_HALL_NUM_CAL_POINTS; i++) {
        if (pt_valid_ & (1 << i)) {
            valid_points.push_back({pt_adc_[i], pt_step_[i]});
        }
    }

    if (valid_points.size() < 2) {
        return NAN;
    }

    // Sort valid points by ADC value ascending
    std::sort(valid_points.begin(), valid_points.end(),
              [](const CalPoint& a, const CalPoint& b) { return a.adc < b.adc; });

    const size_t n = valid_points.size();
    float dial = NAN;

    if (raw_adc <= valid_points[0].adc) {
        // Below minimum calibrated ADC: extrapolate from first segment
        const CalPoint& p0 = valid_points[0];
        const CalPoint& p1 = valid_points[1];
        if (p1.adc != p0.adc) {
            dial = p0.dial + (static_cast<float>(raw_adc - p0.adc) /
                              static_cast<float>(p1.adc - p0.adc)) * (p1.dial - p0.dial);
        } else {
            dial = p0.dial;
        }
    } else if (raw_adc >= valid_points[n - 1].adc) {
        // Above maximum calibrated ADC: extrapolate from last segment
        const CalPoint& p0 = valid_points[n - 2];
        const CalPoint& p1 = valid_points[n - 1];
        if (p1.adc != p0.adc) {
            dial = p0.dial + (static_cast<float>(raw_adc - p0.adc) /
                              static_cast<float>(p1.adc - p0.adc)) * (p1.dial - p0.dial);
        } else {
            dial = p1.dial;
        }
    } else {
        // Piecewise linear interpolation within the matching segment [i, i+1]
        for (size_t i = 0; i < n - 1; i++) {
            const CalPoint& p0 = valid_points[i];
            const CalPoint& p1 = valid_points[i + 1];
            if (raw_adc >= p0.adc && raw_adc <= p1.adc) {
                if (p1.adc != p0.adc) {
                    dial = p0.dial + (static_cast<float>(raw_adc - p0.adc) /
                                      static_cast<float>(p1.adc - p0.adc)) * (p1.dial - p0.dial);
                } else {
                    dial = p0.dial;
                }
                break;
            }
        }
    }

    if (std::isnan(dial)) {
        return NAN;
    }

    // Clamp strictly to [0.0, 5.0] at endpoints ? naa because the dial on the dial is not necessary the real 0, plus the people may turn more than 5
    //if (dial < 0.0f) dial = 0.0f;
    //if (dial > 5.0f) dial = 5.0f;

    return dial;
}

bool HallSensor::record_calibration_point(int point_index, float dial_value) {
    if (point_index < 0 || point_index >= HW_HALL_NUM_CAL_POINTS) {
        return false;
    }

    if (dial_value < 0.0f) {
        dial_value = static_cast<float>(point_index);
    }

    // Reset EMA filter so calibration captures instant reading at this setting
    has_filtered_adc_ = false;
    read_raw_adc();

    pt_adc_[point_index] = last_raw_adc_;
    pt_step_[point_index] = dial_value;
    pt_valid_ |= (1 << point_index);

    save_calibration();
    last_dial_ = calculate_dial(last_raw_adc_);

    LOG_BLE("[HallSensor] Recorded point %d (Dial %.1f): ADC %ld\n",
            point_index + 1, dial_value, last_raw_adc_);
    return true;
}

bool HallSensor::set_calibration_point(int point_index, int32_t raw_adc, float dial_value) {
    if (point_index < 0 || point_index >= HW_HALL_NUM_CAL_POINTS) {
        return false;
    }

    pt_adc_[point_index] = raw_adc;
    pt_step_[point_index] = dial_value;
    pt_valid_ |= (1 << point_index);

    save_calibration();
    last_dial_ = calculate_dial(last_raw_adc_);
    return true;
}

void HallSensor::clear_calibration() {
    Preferences prefs;
    if (prefs.begin(HW_HALL_PREFS_NS, false)) {
        prefs.clear();
        prefs.end();
    }

    pt_valid_ = 0;
    for (int i = 0; i < HW_HALL_NUM_CAL_POINTS; i++) {
        pt_adc_[i] = 0;
        pt_step_[i] = static_cast<float>(i);
    }
    last_dial_ = NAN;
    has_filtered_adc_ = false;

    LOG_BLE("[HallSensor] Calibration cleared\n");
}

bool HallSensor::is_point_calibrated(int point_index) const {
    if (point_index < 0 || point_index >= HW_HALL_NUM_CAL_POINTS) {
        return false;
    }
    return (pt_valid_ & (1 << point_index)) != 0;
}

int32_t HallSensor::get_point_adc(int point_index) const {
    if (point_index < 0 || point_index >= HW_HALL_NUM_CAL_POINTS) {
        return 0;
    }
    return pt_adc_[point_index];
}

float HallSensor::get_point_dial(int point_index) const {
    if (point_index < 0 || point_index >= HW_HALL_NUM_CAL_POINTS) {
        return 0.0f;
    }
    return pt_step_[point_index];
}

int HallSensor::get_calibrated_point_count() const {
    int count = 0;
    for (int i = 0; i < HW_HALL_NUM_CAL_POINTS; i++) {
        if (pt_valid_ & (1 << i)) {
            count++;
        }
    }
    return count;
}

void HallSensor::load_calibration() {
    Preferences prefs;
    if (prefs.begin(HW_HALL_PREFS_NS, true)) {
        pt_valid_ = prefs.getUChar("pt_valid", 0);
        for (int i = 0; i < HW_HALL_NUM_CAL_POINTS; i++) {
            char key_adc[16];
            char key_step[16];
            snprintf(key_adc, sizeof(key_adc), "pt_adc_%d", i);
            snprintf(key_step, sizeof(key_step), "pt_step_%d", i);
            pt_adc_[i] = prefs.getInt(key_adc, 0);
            pt_step_[i] = prefs.getFloat(key_step, static_cast<float>(i));
        }
        prefs.end();
        LOG_BLE("[HallSensor] Loaded calibration from NVS (mask 0x%02X)\n", pt_valid_);
    } else {
        pt_valid_ = 0;
        for (int i = 0; i < HW_HALL_NUM_CAL_POINTS; i++) {
            pt_adc_[i] = 0;
            pt_step_[i] = static_cast<float>(i);
        }
    }
}

void HallSensor::save_calibration() {
    Preferences prefs;
    if (prefs.begin(HW_HALL_PREFS_NS, false)) {
        prefs.putUChar("pt_valid", pt_valid_);
        for (int i = 0; i < HW_HALL_NUM_CAL_POINTS; i++) {
            char key_adc[16];
            char key_step[16];
            snprintf(key_adc, sizeof(key_adc), "pt_adc_%d", i);
            snprintf(key_step, sizeof(key_step), "pt_step_%d", i);
            prefs.putInt(key_adc, pt_adc_[i]);
            prefs.putFloat(key_step, pt_step_[i]);
        }
        prefs.end();
        LOG_BLE("[HallSensor] Saved calibration to NVS (mask 0x%02X)\n", pt_valid_);
    } else {
        LOG_BLE("[HallSensor] ERROR: Failed to open Preferences for saving\n");
    }
}
