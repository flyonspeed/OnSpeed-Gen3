#pragma once

/**
 * Savitzky-Golay First Derivative Filter
 *
 * Minimal implementation for computing smoothed first derivative.
 * Uses quadratic/cubic polynomial fit with configurable window size.
 *
 * Based on Savitzky-Golay convolution coefficients for first derivative.
 * Reference: Savitzky & Golay, Analytical Chemistry, 1964
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
        , _bufferIndex(0)
    {
        // Initialize buffer to zero
        for (int i = 0; i < MAX_WINDOW; i++) {
            _buffer[i] = 0.0;
        }

        // Select coefficients based on window size
        switch (windowSize) {
            case 5:  _coeffs = _coeffs5;  _norm = 10;   break;
            case 7:  _coeffs = _coeffs7;  _norm = 28;   break;
            case 9:  _coeffs = _coeffs9;  _norm = 60;   break;
            case 11: _coeffs = _coeffs11; _norm = 110;  break;
            case 13: _coeffs = _coeffs13; _norm = 182;  break;
            case 15: _coeffs = _coeffs15; _norm = 280;  break;
            case 17: _coeffs = _coeffs17; _norm = 408;  break;
            case 19: _coeffs = _coeffs19; _norm = 570;  break;
            case 21: _coeffs = _coeffs21; _norm = 770;  break;
            case 23: _coeffs = _coeffs23; _norm = 1012; break;
            case 25: _coeffs = _coeffs25; _norm = 1300; break;
            default: _coeffs = _coeffs15; _norm = 280;  _windowSize = 15; break;
        }
    }

    /**
     * Compute the smoothed first derivative
     * @return Derivative value (per sample), or 0.0 if buffer not yet filled
     */
    float Compute() {
        double newValue = *_input;

        // Fill buffer until we have enough samples
        int halfWindow = (_windowSize + 1) / 2;
        if (_fillCount < halfWindow) {
            _buffer[_fillCount] = newValue;
            _fillCount++;
            return 0.0f;
        }

        // Compute derivative using antisymmetric coefficients
        // For first derivative: sum of coeff[i] * (buffer[half+i] - buffer[half-i])
        // This gives mathematically correct sign: positive for increasing input
        // (Unlike SavLayFilter which used past-future and returned negative for increasing)
        double sum = 0.0;
        int half = _windowSize / 2;

        for (int i = 1; i <= half; i++) {
            sum += _coeffs[i] * (_buffer[half + i] - _buffer[half - i]);
        }
        // Center point contributes 0 for first derivative (coeff * 0)

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
    int _bufferIndex;
    double _buffer[MAX_WINDOW];
    const int* _coeffs;
    double _norm;

    // Quadratic first derivative coefficients (antisymmetric)
    // Only store positive side; negative side is negated
    // Index 0 unused, coeffs[1] pairs with ±1 from center, etc.
    static constexpr int _coeffs5[3]   = {0, 1, 2};
    static constexpr int _coeffs7[4]   = {0, 1, 2, 3};
    static constexpr int _coeffs9[5]   = {0, 1, 2, 3, 4};
    static constexpr int _coeffs11[6]  = {0, 1, 2, 3, 4, 5};
    static constexpr int _coeffs13[7]  = {0, 1, 2, 3, 4, 5, 6};
    static constexpr int _coeffs15[8]  = {0, 1, 2, 3, 4, 5, 6, 7};
    static constexpr int _coeffs17[9]  = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    static constexpr int _coeffs19[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    static constexpr int _coeffs21[11] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    static constexpr int _coeffs23[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    static constexpr int _coeffs25[13] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
};

} // namespace onspeed
