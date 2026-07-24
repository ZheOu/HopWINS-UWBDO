/**
 * @file:     deca_rsl.c
 *
 * @brief     This file contains the receive signal strength computations
 *
 * @copyright SPDX-FileCopyrightText: Copyright (c) 2024 Qorvo US, Inc.
 *            SPDX-License-Identifier: LicenseRef-QORVO-2
 *
 */
#include <stdbool.h>
#include <limits.h>
#include <stdint.h>

#include "deca_device_api.h"
#include "deca_rsl.h"

#define ALPHA_IP_PRF_16_Q8 29133L
#define ALPHA_IP_PRF_64_Q8 31155L

#define LOG2_Q_SHIFT 15U
#define LOG2_10_Q15  UINT32_C(108852)
#define Q8_SHIFT     8U

static uint32_t log2_q15(uint32_t value)
{
    uint32_t exponent = 31U - (uint32_t)__builtin_clz(value);
    uint64_t normalized = (uint64_t)value << (31U - exponent);
    uint32_t fraction = 0U;

    for (int32_t bit = (int32_t)LOG2_Q_SHIFT - 1; bit >= 0; bit--)
    {
        normalized = (normalized * normalized) >> 31U;
        if (normalized >= (UINT64_C(1) << 32U))
        {
            normalized >>= 1U;
            fraction |= UINT32_C(1) << (uint32_t)bit;
        }
    }

    return (exponent << LOG2_Q_SHIFT) | fraction;
}

/**
 * rsl_calculate() - Estimate the signal power in dBm
 *
 * @power: power as integer.
 * @n: number of accumulate symbols or STS length.
 * @pow2: power of 2 to be multiplied to @power.
 *
 * Return: estimated signal power as a signed q8.8, with 0.1 dBm of precision,
 *         SHRT_MIN or error (-128.0).
 *
 * References:
 *  [1] DW3000 Family User Manual, sections 4.7.1, 4.7.2, version 1.1
 *  [2] DW3700 User Manual v0.4 sections 4.7.1, 4.7.2, version 0.4
 *
 *   10 * log10(power * 2^pow2 / n²) + 6 * D - A
 *
 * For the signal power in the first path
 *
 *   power = F1² + F2² + F3²
 *   pow2 = 0
 *
 *   F1, the First Path Amplitude (point 1) magnitude value (2 fractional bits).
 *   F2, the First Path Amplitude (point 2) magnitude value (2 fractional bits).
 *   F3, the First Path Amplitude (point 3) magnitude value (2 fractional bits).
 *
 * For the received signal
 *
 *   power = C, pow2 = 21  on the DW3 C0 [1]
 *   power = C, pow2 = 17  on the DW3 D0 and E0 [2]
 *
 *   C, the Channel Impulse Response Power value.
 *
 * Remaining parameters are common.
 *
 *   D, the DGC_DECISION, treated as an unsigned integer in range 0 to 7.
 *      0 when RX_TUNE_EN bit is not set in DGC_CFG (No DGC).
 *
 *   n, the number of preamble symbols accumulated, or accumulated STS length.
 *
 *   A, the constant.
 *      113.8 for a PRF of 16 MHz, or                   [1]
 *      121.7 for a PRF of 64 MHz Ipatov preamble or    [1]
 *      120.7 for a PRF of 64 MHz STS or                [1]
 */
static int16_t rsl_calculate(
    uint32_t cir_power,
    uint32_t symbol_count,
    uint32_t power_of_two,
    uint8_t dgc_decision,
    uint8_t rx_pcode,
    bool is_sts)
{
    /* Algo to simplify the log computation:
     *      log10((power*2^pow2)/(n*n)) = log2((power*2^pow2)/(n*n))/log2(10)
     *      log10((power*2^pow2)/(n*n)) = (log(2^pow2) + log2(power) - log2(n*n))/log2(10)
     *      log10((power*2^pow2)/(n*n)) = (pow2 + log2(power) - 2*log2(n))/log2(10)
     * log2_q15() keeps the logarithm in Q15. The final conversion multiplies
     * by 10 * 256 and divides by log2(10) in Q15 to produce signed Q8.8.
     */
    int64_t log_ratio_q15;
    int64_t signal_power_q8;
    int32_t alpha_q8;

    if ((cir_power == 0U) || (symbol_count == 0U))
    {
        return (int16_t)SHRT_MIN;
    }

    log_ratio_q15 =
        ((int64_t)power_of_two << LOG2_Q_SHIFT) +
        (int64_t)log2_q15(cir_power) -
        (2LL * (int64_t)log2_q15(symbol_count));
    signal_power_q8 =
        (log_ratio_q15 * 10LL * (INT64_C(1) << Q8_SHIFT)) /
        (int64_t)LOG2_10_Q15;

    /* Computation of the offsets. */
    if (PCODE_PRF64_START <= rx_pcode)
    {
        alpha_q8 = ALPHA_IP_PRF_64_Q8;
        if (is_sts)
        {
            alpha_q8 -= (int32_t)(UINT32_C(1) << Q8_SHIFT);
        }
    }
    else
    {
        alpha_q8 = ALPHA_IP_PRF_16_Q8;
    }

    signal_power_q8 +=
        ((int64_t)dgc_decision * 6LL * (INT64_C(1) << Q8_SHIFT)) -
        alpha_q8;

    if (signal_power_q8 < SHRT_MIN)
    {
        return (int16_t)SHRT_MIN;
    }
    if (signal_power_q8 > SHRT_MAX)
    {
        return (int16_t)SHRT_MAX;
    }

    return (int16_t)signal_power_q8;
}

int16_t rsl_calculate_signal_power(
    int32_t channel_impulse_response,
    uint8_t quantization_factor,
    uint16_t preamble_accumulation_count,
    uint8_t dgc_decision,
    uint8_t rx_pcode,
    bool is_sts
)
{
    return rsl_calculate(
        (uint32_t)channel_impulse_response,
        preamble_accumulation_count,
        quantization_factor,
        dgc_decision,
        rx_pcode,
        is_sts);
}

int16_t rsl_calculate_first_path_power(
    uint32_t f1,
    uint32_t f2,
    uint32_t f3,
    uint16_t preamble_accumulation_count,
    uint8_t dgc_decision,
    uint8_t rx_pcode,
    bool is_sts
)
{
    uint32_t channel_area;

    f1 /= 4U;
    f2 /= 4U;
    f3 /= 4U;
    channel_area = (f1 * f1) + (f2 * f2) + (f3 * f3);

    return rsl_calculate(
        channel_area,
        preamble_accumulation_count,
        0U,
        dgc_decision,
        rx_pcode,
        is_sts);
}
