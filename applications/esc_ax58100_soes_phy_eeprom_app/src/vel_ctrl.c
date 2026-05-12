#include "vel_ctrl.h"

#include <limits.h>

#include "enc_dma.h"
#include "pwm_dma.h"

#define VEL_CTRL_SHIFT 12
typedef struct
{
    uint8_t enabled;
    int32_t host_command;
    int32_t previous_command;
    int32_t integrator;
    int32_t integrator_limit;
    int32_t output_limit;
    int32_t kp;
    int32_t ki;
    int32_t ff0;
    int32_t ff1;
    int32_t output_command;
} vel_ctrl_axis_t;

static vel_ctrl_axis_t g_vel_ctrl[VEL_CTRL_CHANNELS];

static int32_t vel_ctrl_clamp(int32_t value, int32_t low, int32_t high)
{
    if (value < low)
    {
        return low;
    }

    if (value > high)
    {
        return high;
    }

    return value;
}

void vel_ctrl_init(void)
{
    for (uint8_t axis = 0; axis < VEL_CTRL_CHANNELS; ++axis)
    {
        g_vel_ctrl[axis].enabled = 1U;
        g_vel_ctrl[axis].host_command = 0;
        g_vel_ctrl[axis].previous_command = 0;
        g_vel_ctrl[axis].integrator = 0;
        g_vel_ctrl[axis].integrator_limit = PWM_DMA_COMMAND_LIMIT;
        g_vel_ctrl[axis].output_limit = PWM_DMA_COMMAND_LIMIT;
        g_vel_ctrl[axis].kp = 0;
        g_vel_ctrl[axis].ki = 0;
        g_vel_ctrl[axis].ff0 = 0;
        g_vel_ctrl[axis].ff1 = 0;
        g_vel_ctrl[axis].output_command = 0;
    }
}

void vel_ctrl_set_enabled(uint8_t axis, uint8_t enabled)
{
    if (axis >= VEL_CTRL_CHANNELS)
    {
        return;
    }

    vel_ctrl_axis_t *state = &g_vel_ctrl[axis];

    if ((enabled != 0U) && (state->enabled == 0U))
    {
        state->previous_command = state->host_command;
    }

    if (enabled == 0U)
    {
        state->integrator = 0;
        state->output_command = 0;
    }

    state->enabled = enabled ? 1U : 0U;
}

void vel_ctrl_set_host_command(uint8_t axis, int32_t command)
{
    if (axis >= VEL_CTRL_CHANNELS)
    {
        return;
    }

    g_vel_ctrl[axis].host_command = command;
}

void vel_ctrl_set_gains(uint8_t axis, int32_t kp, int32_t ki, int32_t ff0, int32_t ff1)
{
    if (axis >= VEL_CTRL_CHANNELS)
    {
        return;
    }

    g_vel_ctrl[axis].kp = kp;
    g_vel_ctrl[axis].ki = ki;
    g_vel_ctrl[axis].ff0 = ff0;
    g_vel_ctrl[axis].ff1 = ff1;
}

void vel_ctrl_set_limits(uint8_t axis, int32_t integrator_limit, int32_t output_limit)
{
    if (axis >= VEL_CTRL_CHANNELS)
    {
        return;
    }

    g_vel_ctrl[axis].integrator_limit = (integrator_limit > 0) ? integrator_limit : 0;
    g_vel_ctrl[axis].output_limit = (output_limit > 0) ? output_limit : 0;
}

void vel_ctrl_fast_tick(void)
{
    for (uint8_t axis = 0; axis < VEL_CTRL_CHANNELS; ++axis)
    {
        vel_ctrl_axis_t *state = &g_vel_ctrl[axis];

        if (state->enabled == 0U)
        {
            state->output_command = 0;
            continue;
        }

        int32_t velocity_feedback = enc_dma_get_velocity(axis);
        int32_t command_delta = state->host_command - state->previous_command;
        int32_t error = state->host_command - velocity_feedback;
        int32_t proportional = (state->kp * error) >> VEL_CTRL_SHIFT;
        int32_t ff0_term = (state->ff0 * state->host_command) >> VEL_CTRL_SHIFT;
        int32_t ff1_term = (state->ff1 * command_delta) >> VEL_CTRL_SHIFT;
        int32_t integrator_candidate = vel_ctrl_clamp(
            state->integrator + ((state->ki * error) >> VEL_CTRL_SHIFT),
            -state->integrator_limit,
            state->integrator_limit);
        int32_t output_candidate = proportional + integrator_candidate + ff0_term + ff1_term;
        int32_t output_saturated = vel_ctrl_clamp(output_candidate, -state->output_limit, state->output_limit);

        if ((output_candidate == output_saturated)
            || ((output_candidate > state->output_limit) && (error < 0))
            || ((output_candidate < -state->output_limit) && (error > 0)))
        {
            state->integrator = integrator_candidate;
        }

        state->output_command = vel_ctrl_clamp(
            proportional + state->integrator + ff0_term + ff1_term,
            -state->output_limit,
            state->output_limit);

        state->previous_command = state->host_command;
    }
}

int32_t vel_ctrl_get_output(uint8_t axis)
{
    if (axis >= VEL_CTRL_CHANNELS)
    {
        return 0;
    }

    return g_vel_ctrl[axis].output_command;
}