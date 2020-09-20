/** dude, is my code constant time?
 *
 * This file measures the execution time of a given function many times with
 * different inputs and performs a Welch's t-test to determine if the function
 * runs in constant time or not. This is essentially leakage detection, and
 * not a timing attack.
 *
 * Notes:
 *
 *  - the execution time distribution tends to be skewed towards large
 *    timings, leading to a fat right tail. Most executions take little time,
 *    some of them take a lot. We try to speed up the test process by
 *    throwing away those measurements with large cycle count. (For example,
 *    those measurements could correspond to the execution being interrupted
 *    by the OS.) Setting a threshold value for this is not obvious; we just
 *    keep the x% percent fastest timings, and repeat for several values of x.
 *
 *  - the previous observation is highly heuristic. We also keep the uncropped
 *    measurement time and do a t-test on that.
 *
 *  - we also test for unequal variances (second order test), but this is
 *    probably redundant since we're doing as well a t-test on cropped
 *    measurements (non-linear transform)
 *
 *  - as long as any of the different test fails, the code will be deemed
 *    variable time.
 *
 */

#include "fixture.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../console.h"
#include "../random.h"
#include "constant.h"
#include "ttest.h"

extern int enough_measurements;

extern const int drop_size;
extern const size_t chunk_size;
extern const size_t number_measurements;
static t_ctx *t;

/* threshold values for Welch's t-test */
#define t_threshold_bananas                                                  \
    500                         /* Test failed with overwhelming probability \
                                 */
#define t_threshold_moderate 10 /* Test failed */

static void __attribute__((noreturn)) die(void)
{
    exit(111);
}

static void differentiate(int64_t *exec_times,
                          int64_t *before_ticks,
                          int64_t *after_ticks)
{
    for (size_t i = 0; i < number_measurements; i++) {
        exec_times[i] = after_ticks[i] - before_ticks[i];
    }
}

static void update_statistics(int64_t *exec_times, uint8_t *classes)
{
    for (size_t i = 0; i < number_measurements; i++) {
        int64_t difference = exec_times[i];
        /* Cpu cycle counter overflowed or dropped measurement */
        if (difference <= 0) {
            continue;
        }
        /* do a t-test on the execution time */
        t_push(t, difference, classes[i]);
    }
}

static bool report(void)
{
    double max_t = fabs(t_compute(t));
    double number_traces_max_t = t->n[0] + t->n[1];
    double max_tau = max_t / sqrt(number_traces_max_t);

    printf("\033[A\033[2K\033[A\033[2K");
    printf("meas: %7.2lf M, ", (number_traces_max_t / 1e6));
    if (number_traces_max_t < enough_measurements) {
        printf("not enough measurements (%.0f still to go).\n",
               enough_measurements - number_traces_max_t);
    } else {
        printf("\n");
    }

    /*
     * max_t: the t statistic value
     * max_tau: a t value normalized by sqrt(number of measurements).
     *          this way we can compare max_tau taken with different
     *          number of measurements. This is sort of "distance
     *          between distributions", independent of number of
     *          measurements.
     * (5/tau)^2: how many measurements we would need to barely
     *            detect the leak, if present. "barely detect the
     *            leak" = have a t value greater than 5.
     */
    printf(
        "max t: %+7.2f, max tau: %.2e, (5/tau)^2: %.2e, mu0: %.2e, mu1: %.2e, "
        "dmu: %-.2e, "
        "s0: %.2e, s1: %.2e, m20: %.2e, m21: %.2e.\n",
        max_t, max_tau, (double) (5 * 5) / (double) (max_tau * max_tau),
        t->mean[0], t->mean[1], t->mean[1] - t->mean[0],
        sqrt(t->m2[0] / (t->n[0] - 1)), sqrt(t->m2[0] / (t->n[0] - 1)),
        t->m2[0], t->m2[1]);

    if (max_t > t_threshold_bananas) {
        return false;
    } else if (max_t > t_threshold_moderate) {
        return false;
    } else { /* max_t < t_threshold_moderate */
        return true;
    }
}

static bool doit(int mode)
{
    int64_t *before_ticks = calloc(number_measurements + 1, sizeof(int64_t));
    int64_t *after_ticks = calloc(number_measurements + 1, sizeof(int64_t));
    int64_t *exec_times = calloc(number_measurements, sizeof(int64_t));
    uint8_t *classes = calloc(number_measurements, sizeof(uint8_t));
    uint8_t *input_data =
        calloc(number_measurements * chunk_size, sizeof(uint8_t));

    if (!before_ticks || !after_ticks || !exec_times || !classes ||
        !input_data) {
        die();
    }

    prepare_inputs(input_data, classes);

    measure(before_ticks, after_ticks, input_data, classes, mode);
    differentiate(exec_times, before_ticks, after_ticks);
    update_statistics(exec_times, classes);
    bool ret = report();

    free(before_ticks);
    free(after_ticks);
    free(exec_times);
    free(classes);
    free(input_data);

    return ret;
}

static void init_once(void)
{
    t_init(t);
}

bool is_insert_tail_const(void)
{
    bool result = false;
    t = malloc(sizeof(t_ctx));

    printf("Testing insert_tail...\n\n\n");
    init_once();
    for (int i = 0;
         i < enough_measurements / (number_measurements - drop_size * 2) + 1;
         ++i)
        result = doit(0);
    free(t);
    return result;
}

bool is_size_const(void)
{
    bool result = false;
    t = malloc(sizeof(t_ctx));
    printf("Testing size...\n\n\n");
    init_once();
    for (int i = 0;
         i < enough_measurements / (number_measurements - drop_size * 2) + 1;
         ++i)
        result = doit(1);
    free(t);
    return result;
}
