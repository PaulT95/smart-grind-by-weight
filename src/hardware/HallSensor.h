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
     * @brief Get the last read raw ADC value.
     */
    int32_t get_raw_adc() const { return last_raw_adc_; }

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
    float last_dial_;
    float filtered_adc_;
    bool has_filtered_adc_;
    bool initialized_;
};
