#pragma once

/**
 * Savitzky-Golay First Derivative Filter
 *
 * Correct implementation of S-G first derivative using quadratic polynomial fit.
 * Returns the exact derivative for linear input (slope 1 -> output 1).
 *
 * Effective coefficients: [-half, ..., -1, 0, +1, ..., +half]
 * Normalization: sum of i^2 for i=1 to half, times 2 = (half)(half+1)(2*half+1)/3
 *
 * Reference: Savitzky & Golay, Analytical Chemistry, 1964
 * Verified against scipy.signal.savgol_coeffs
 */

namespace onspeed {

class SavGolDerivative {
public:
    /**
     * Constructor
     * @param input Pointer to the input variable to track
     * @param windowSize Window size (must be odd: 5,7,9,11,13,15,17,19,21,23,25)
     */
    SavGolDerivative(double* input, int windowSize)
        : _input(input)
        , _windowSize(windowSize)
        , _fillCount(0)
    {
        // Initialize buffer to zero
        for (int i = 0; i < MAX_WINDOW; i++) {
            _buffer[i] = 0.0;
        }

        // Validate and set window size
        if (windowSize < 5 || windowSize > 25 || (windowSize % 2) == 0) {
            _windowSize = 15;  // Default to 15 for invalid sizes
        }

        // Compute normalization factor: 2 * sum(i^2) for i=1 to half
        // = 2 * (half)(half+1)(2*half+1)/6 = (half)(half+1)(2*half+1)/3
        int half = _windowSize / 2;
        _norm = static_cast<double>(half * (half + 1) * (2 * half + 1)) / 3.0;
    }

    /**
     * Compute the smoothed first derivative
     * @return Derivative value (per sample), or 0.0 if buffer not yet filled
     *
     * Sign convention: positive output for increasing input
     */
    float Compute() {
        double newValue = *_input;

        // Fill buffer until we have a complete window
        if (_fillCount < _windowSize) {
            _buffer[_fillCount] = newValue;
            _fillCount++;
            return 0.0f;
        }

        // Compute derivative using correct antisymmetric S-G coefficients
        // Coefficient for position (center + i) is -i, for (center - i) is +i
        // This gives: sum of i * (buffer[half-i] - buffer[half+i]) for i=1 to half
        double sum = 0.0;
        int half = _windowSize / 2;

        for (int i = 1; i <= half; i++) {
            // Coefficient i multiplies (future - past)
            // this adds two terms at once to the sum
            sum += i * (_buffer[half + i] - _buffer[half - i]);
        }

        float result = static_cast<float>(sum / _norm);

        // Shift buffer left and add new value
        for (int i = 0; i < _windowSize - 1; i++) {
            _buffer[i] = _buffer[i + 1];
        }
        _buffer[_windowSize - 1] = newValue;

        return result;
    }

    /**
     * Reset the filter state
     */
    void reset() {
        _fillCount = 0;
        for (int i = 0; i < MAX_WINDOW; i++) {
            _buffer[i] = 0.0;
        }
    }

private:
    static constexpr int MAX_WINDOW = 25;

    double* _input;
    int _windowSize;
    int _fillCount;
    double _buffer[MAX_WINDOW];
    double _norm;
};

} // namespace onspeed
