#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include "../config/constants.h"

/**
 * @brief KY-024 Linear Hall-Effect Sensor driver for grind knob tracking.
 * 
 * Maps raw ADC values to physical dial numbers (0.0 - 5.0) using up to 6-point
 * piecewise linear calibration with NVS persistence under the "hall" namespace.
 */
class HallSensor {
public:
    static constexpr int kNumCalPoints = HW_HALL_NUM_CAL_POINTS;

    HallSensor();
    ~HallSensor();

    /**
     * @brief Initialize hardware pin and load calibration from NVS.
     * @param preferences Optional Preferences pointer.
     */
    void init(Preferences* preferences = nullptr);

    /**
     * @brief Read averaged raw ADC value from the sensor.
     * Takes HW_HALL_NUM_SAMPLES readings with a 1ms delay between samples.
     * Updates cached raw ADC and dial position.
     * @return Averaged raw ADC value.
     */
    int32_t read_raw_adc();

    /**
     * @brief Calculate dial number from a raw ADC reading using piecewise linear calibration.
     * Formula: dial = dial_i + ((raw - adc_i) / (adc_{i+1} - adc_i)) * (dial_{i+1} - dial_i)
     * At moment no clamping: Clamped to [1.0, 5.0] at endpoints, returning NAN if fewer than 2 points are calibrated.
     * @param raw_adc Raw ADC value.
     * @return Interpolated dial position (0.0 - 5.0), or NAN if fewer than 2 points calibrated.
     */
    float calculate_dial(int32_t raw_adc) const;

    /**
     * @brief Get the last computed dial position.
     * @return Dial position (1.0 - 5.0), or NAN if uncalibrated.
     */
    float get_dial_position() const { return last_dial_; }

    /**
     * @brief Get the last read raw (uncompensated) ADC value.
     */
    int32_t get_raw_adc() const { return last_raw_adc_; }

    /**
     * @brief Get the last temperature-compensated ADC value.
     */
    int32_t get_comp_adc() const { return last_comp_adc_; }

    /**
     * @brief Get the last read ESP32 internal chip temperature in degrees C.
     */
    float get_chip_temperature() const { return last_temp_c_; }

    /**
     * @brief Check if temperature drift compensation is enabled.
     */
    bool is_temp_comp_enabled() const { return temp_comp_enabled_; }

    /**
     * @brief Enable or disable temperature drift compensation.
     */
    void set_temp_comp_enabled(bool enabled) { temp_comp_enabled_ = enabled; }

    /**
     * @brief Get temperature coefficient (ADC counts per deg C).
     */
    float get_temp_coeff() const { return temp_coeff_; }

    /**
     * @brief Set temperature coefficient (ADC counts per deg C).
     */
    void set_temp_coeff(float coeff) { temp_coeff_ = coeff; }

    /**
     * @brief Get reference calibration temperature in deg C.
     */
    float get_temp_ref() const { return temp_ref_; }

    /**
     * @brief Set reference calibration temperature in deg C.
     */
    void set_temp_ref(float temp_ref) { temp_ref_ = temp_ref; }

    /**
     * @brief Read the current ESP32 die temperature in degrees C.
     */
    static float read_chip_temperature();

    /**
     * @brief Drift calibration status and regression metrics.
     */
    struct DriftCalStatus {
        bool in_progress = false;
        bool is_complete = false;
        uint32_t elapsed_ms = 0;
        int sample_count = 0;
        float start_temp = 0.0f;
        float current_temp = 0.0f;
        int32_t start_adc = 0;
        int32_t current_adc = 0;
        float calculated_coeff = 0.0f;
        bool has_valid_fit = false;
    };

    /**
     * @brief Start automated drift calibration sampling.
     */
    void start_drift_calibration();

    /**
     * @brief Update automated drift calibration (call periodically).
     * @return True if a sample point was recorded.
     */
    bool update_drift_calibration();

    /**
     * @brief Check if drift calibration is currently running.
     */
    bool is_drift_cal_in_progress() const { return drift_cal_active_; }

    /**
     * @brief Query current drift calibration status and regression fit.
     */
    DriftCalStatus get_drift_cal_status() const;

    /**
     * @brief Finish drift calibration, optionally saving the fit to NVS.
     * @param save True to apply and persist the calculated coefficient.
     * @return True if a valid fit was saved.
     */
    bool finish_drift_calibration(bool save);

    /**
     * @brief Cancel active drift calibration without saving.
     */
    void cancel_drift_calibration();

    /**
     * @brief Record current ADC reading for a given calibration point.
     * @param point_index Point index (0 to 5, representing dial 0.0 to 5.0).
     * @param dial_value Optional explicit dial value (defaults to point_index).
     * @return True if recorded and saved successfully.
     */
    bool record_calibration_point(int point_index, float dial_value = -1.0f);

    /**
     * @brief Set specific calibration point values directly.
     * @param point_index Point index (0 to 5).
     * @param raw_adc Raw ADC reading.
     * @param dial_value Corresponding dial position.
     * @return True if point index is valid.
     */
    bool set_calibration_point(int point_index, int32_t raw_adc, float dial_value);

    /**
     * @brief Clear all calibration data from RAM and NVS.
     */
    void clear_calibration();

    /**
     * @brief Check if a specific calibration point is recorded.
     * @param point_index Point index (0 to 4).
     */
    bool is_point_calibrated(int point_index) const;

    /**
     * @brief Get ADC reading for a calibration point.
     */
    int32_t get_point_adc(int point_index) const;

    /**
     * @brief Get dial value for a calibration point.
     */
    float get_point_dial(int point_index) const;

    /**
     * @brief Get the raw bitmask of valid calibration points.
     */
    uint8_t get_valid_mask() const { return pt_valid_; }

    /**
     * @brief Get the count of valid calibrated points.
     */
    int get_calibrated_point_count() const;

    /**
     * @brief Check whether at least 2 points are calibrated.
     */
    bool is_calibrated() const { return get_calibrated_point_count() >= 2; }

    /**
     * @brief Load calibration data from NVS Preferences ("hall" namespace).
     */
    void load_calibration();

    /**
     * @brief Save calibration data to NVS Preferences ("hall" namespace).
     */
    void save_calibration();

private:
    int32_t pt_adc_[HW_HALL_NUM_CAL_POINTS];
    float pt_step_[HW_HALL_NUM_CAL_POINTS];
    uint8_t pt_valid_;

    int32_t last_raw_adc_;
    int32_t last_comp_adc_;
    float last_dial_;
    float filtered_adc_;
    bool has_filtered_adc_;
    bool initialized_;

    // Temperature drift compensation
    float last_temp_c_;
    bool temp_comp_enabled_;
    float temp_coeff_;
    float temp_ref_;

    // Live drift calibration sampling & linear regression state
    bool drift_cal_active_;
    uint32_t drift_cal_start_ms_;
    uint32_t drift_cal_last_sample_ms_;
    int drift_sample_count_;
    float drift_start_temp_;
    float drift_min_temp_;
    float drift_max_temp_;
    int32_t drift_start_adc_;
    float drift_sum_t_;
    float drift_sum_adc_;
    float drift_sum_t2_;
    float drift_sum_t_adc_;
    float drift_calculated_coeff_;
    bool drift_has_valid_fit_;
};
