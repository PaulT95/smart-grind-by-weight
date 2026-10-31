#include "HallSensor.h"
#include <Arduino.h>
#include <cmath>
#include <vector>
#include <algorithm>

HallSensor::HallSensor()
    : pt_valid_(0),
      last_raw_adc_(0),
      last_comp_adc_(0),
      last_dial_(NAN),
      filtered_adc_(0.0f),
      has_filtered_adc_(false),
      initialized_(false),
      last_temp_c_(25.0f),
      temp_comp_enabled_(true),
      temp_coeff_(-0.25f),
      temp_ref_(25.0f),
      drift_cal_active_(false),
      drift_cal_start_ms_(0),
      drift_cal_last_sample_ms_(0),
      drift_sample_count_(0),
      drift_start_temp_(25.0f),
      drift_min_temp_(25.0f),
      drift_max_temp_(25.0f),
      drift_start_adc_(0),
      drift_sum_t_(0.0f),
      drift_sum_adc_(0.0f),
      drift_sum_t2_(0.0f),
      drift_sum_t_adc_(0.0f),
      drift_calculated_coeff_(0.0f),
      drift_has_valid_fit_(false) {
    for (int i = 0; i < HW_HALL_NUM_CAL_POINTS; i++) {
        pt_adc_[i] = 0;
        pt_step_[i] = static_cast<float>(i); //we start at 0 because 0 is when the blade "touches"
    }
}

HallSensor::~HallSensor() = default;

float HallSensor::read_chip_temperature() {
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    return temperatureRead();
#else
    return 25.0f;
#endif
}

void HallSensor::init(Preferences* /* preferences */) {
    pinMode(HW_HALL_ADC_PIN, INPUT);
    load_calibration();
    initialized_ = true;

    LOG_BLE("[HallSensor] Initialized on ADC pin %d (calibrated points: %d/6, temp_comp: %s, coeff: %.2f cnt/C, ref: %.1f C)\n",
            HW_HALL_ADC_PIN, get_calibrated_point_count(),
            temp_comp_enabled_ ? "ON" : "OFF", temp_coeff_, temp_ref_);
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
    last_temp_c_ = read_chip_temperature();

    if (temp_comp_enabled_) {
        float temp_offset = temp_coeff_ * (last_temp_c_ - temp_ref_);
        last_comp_adc_ = static_cast<int32_t>(std::round(static_cast<float>(last_raw_adc_) - temp_offset));
    } else {
        last_comp_adc_ = last_raw_adc_;
    }

    last_dial_ = calculate_dial(last_comp_adc_);
    return last_comp_adc_;
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

    // If starting a fresh calibration, anchor reference temperature to current chip temperature
    if (pt_valid_ == 0) {
        temp_ref_ = last_temp_c_;
        last_comp_adc_ = last_raw_adc_;
    }

    pt_adc_[point_index] = last_comp_adc_;
    pt_step_[point_index] = dial_value;
    pt_valid_ |= (1 << point_index);

    save_calibration();
    last_dial_ = calculate_dial(last_comp_adc_);

    LOG_BLE("[HallSensor] Recorded point %d (Dial %.1f): Raw %ld, Comp %ld @ %.1f C\n",
            point_index + 1, dial_value, last_raw_adc_, last_comp_adc_, last_temp_c_);
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
    last_dial_ = calculate_dial(last_comp_adc_);
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
    temp_comp_enabled_ = true;
    temp_coeff_ = -0.25f;
    temp_ref_ = 25.0f;
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
        temp_comp_enabled_ = prefs.getBool("tc_en", true);
        temp_coeff_ = prefs.getFloat("tc_coeff", -0.25f);
        temp_ref_ = prefs.getFloat("tc_ref", 25.0f);
        prefs.end();
        LOG_BLE("[HallSensor] Loaded calibration from NVS (mask 0x%02X, comp=%d, coeff=%.2f, ref=%.1f C)\n",
                pt_valid_, temp_comp_enabled_, temp_coeff_, temp_ref_);
    } else {
        pt_valid_ = 0;
        for (int i = 0; i < HW_HALL_NUM_CAL_POINTS; i++) {
            pt_adc_[i] = 0;
            pt_step_[i] = static_cast<float>(i);
        }
        temp_comp_enabled_ = true;
        temp_coeff_ = -0.25f;
        temp_ref_ = 25.0f;
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
        prefs.putBool("tc_en", temp_comp_enabled_);
        prefs.putFloat("tc_coeff", temp_coeff_);
        prefs.putFloat("tc_ref", temp_ref_);
        prefs.end();
        LOG_BLE("[HallSensor] Saved calibration to NVS (mask 0x%02X, comp=%d, coeff=%.2f, ref=%.1f C)\n",
                pt_valid_, temp_comp_enabled_, temp_coeff_, temp_ref_);
    } else {
        LOG_BLE("[HallSensor] ERROR: Failed to open Preferences for saving\n");
    }
}

void HallSensor::start_drift_calibration() {
    read_raw_adc();

    drift_cal_active_ = true;
    drift_cal_start_ms_ = millis();
    drift_cal_last_sample_ms_ = 0;
    drift_sample_count_ = 0;
    drift_start_temp_ = last_temp_c_;
    drift_min_temp_ = last_temp_c_;
    drift_max_temp_ = last_temp_c_;
    drift_start_adc_ = last_raw_adc_;
    drift_sum_t_ = 0.0f;
    drift_sum_adc_ = 0.0f;
    drift_sum_t2_ = 0.0f;
    drift_sum_t_adc_ = 0.0f;
    drift_calculated_coeff_ = 0.0f;
    drift_has_valid_fit_ = false;

    LOG_BLE("[HallSensor] Drift calibration started: start_adc=%ld, start_temp=%.1f C\n",
            drift_start_adc_, drift_start_temp_);
}

bool HallSensor::update_drift_calibration() {
    if (!drift_cal_active_) return false;

    uint32_t now = millis();
    constexpr uint32_t kMaxDriftDurationMs = 15 * 60 * 1000;
    if (now - drift_cal_start_ms_ >= kMaxDriftDurationMs) {
        return false;
    }

    if (drift_cal_last_sample_ms_ != 0 && (now - drift_cal_last_sample_ms_ < 5000)) {
        return false;
    }
    drift_cal_last_sample_ms_ = now;

    float t = read_chip_temperature();
    int32_t adc = last_raw_adc_;

    if (t < drift_min_temp_) drift_min_temp_ = t;
    if (t > drift_max_temp_) drift_max_temp_ = t;

    drift_sum_t_ += t;
    drift_sum_adc_ += static_cast<float>(adc);
    drift_sum_t2_ += t * t;
    drift_sum_t_adc_ += t * static_cast<float>(adc);
    drift_sample_count_++;

    float delta_t = drift_max_temp_ - drift_min_temp_;
    if (drift_sample_count_ >= 6 && delta_t >= 1.0f) {
        float n = static_cast<float>(drift_sample_count_);
        float denom = (n * drift_sum_t2_) - (drift_sum_t_ * drift_sum_t_);
        float num = (n * drift_sum_t_adc_) - (drift_sum_t_ * drift_sum_adc_);
        if (std::abs(denom) > 1e-4f) {
            drift_calculated_coeff_ = num / denom;
            drift_has_valid_fit_ = true;
        }
    }

    return true;
}

HallSensor::DriftCalStatus HallSensor::get_drift_cal_status() const {
    DriftCalStatus status;
    status.in_progress = drift_cal_active_;
    constexpr uint32_t kMaxDriftDurationMs = 15 * 60 * 1000;
    uint32_t elapsed = drift_cal_active_ ? (millis() - drift_cal_start_ms_) : 0;
    if (elapsed >= kMaxDriftDurationMs) {
        status.elapsed_ms = kMaxDriftDurationMs;
        status.is_complete = true;
    } else {
        status.elapsed_ms = elapsed;
        status.is_complete = false;
    }
    status.sample_count = drift_sample_count_;
    status.start_temp = drift_start_temp_;
    status.current_temp = last_temp_c_;
    status.start_adc = drift_start_adc_;
    status.current_adc = last_raw_adc_;
    status.calculated_coeff = drift_calculated_coeff_;
    status.has_valid_fit = drift_has_valid_fit_;
    return status;
}

bool HallSensor::finish_drift_calibration(bool save) {
    if (!drift_cal_active_) return false;

    bool saved = false;
    if (save && drift_has_valid_fit_) {
        temp_coeff_ = drift_calculated_coeff_;
        temp_ref_ = drift_start_temp_;
        temp_comp_enabled_ = true;
        save_calibration();
        saved = true;
        LOG_BLE("[HallSensor] Drift calibration saved: coeff=%.3f cnt/C, ref=%.1f C\n",
                temp_coeff_, temp_ref_);
    }

    drift_cal_active_ = false;
    return saved;
}

void HallSensor::cancel_drift_calibration() {
    drift_cal_active_ = false;
    LOG_BLE("[HallSensor] Drift calibration cancelled\n");
}
