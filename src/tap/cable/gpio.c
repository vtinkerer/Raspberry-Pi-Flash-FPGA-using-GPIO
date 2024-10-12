/*
 * Modified by Vladyslav Baida on 12.10.2024
 * Changes: Added the support of gpio with libgpiod
 */

/*
 * (C) Copyright 2010
 * Stefano Babic, DENX Software Engineering, sbabic@denx.de.
 * GPIO JTAG Cable Driver
 *
 * Based on TS7800 GPIO JTAG Cable Driver
 * Copyright (C) 2008 Catalin Ionescu
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.     See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include <sysdep.h>

#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <urjtag/cable.h>
#include <urjtag/parport.h>
#include <urjtag/chain.h>
#include <urjtag/cmd.h>

#include "generic.h"

#include <gpiod.h>

#define GPIO_PATH          "/sys/class/gpio/"
#define GPIO_EXPORT_PATH   GPIO_PATH "export"
#define GPIO_UNEXPORT_PATH GPIO_PATH "unexport"

/* pin mapping */
enum {
    GPIO_UNSET = -1,
    GPIO_TDI = 0,
    GPIO_TCK,
    GPIO_TMS,
    GPIO_TDO,
    GPIO_REQUIRED
};

typedef struct {
    unsigned int                jtag_gpios[4];
    int                         signals;
    uint32_t                    lastout;
    struct gpiod_line_request   *request;
} gpio_params_t;

static int gpio_set_value (urj_cable_t *cable, int pin, int value)
{
    int ret;

    gpio_params_t *p = cable->params;

    ret = gpiod_line_request_set_value(p->request, pin, value);
    if (ret) {
        return URJ_STATUS_FAIL;
    }

    return URJ_STATUS_OK;
}


static int gpio_get_value (urj_cable_t *cable, int pin) 
{
    gpio_params_t *p = cable->params;

    return gpiod_line_request_get_value(p->request, pin);
}


static int 
gpio_open(urj_cable_t *cable) 
{
    int i, ret;
    gpio_params_t *p = cable->params;

    struct gpiod_line_request *request = NULL;
    struct gpiod_chip *chip = NULL;
    struct gpiod_line_config *line_cfg = NULL;
    struct gpiod_request_config *rconfig = NULL;

    chip = gpiod_chip_open("/dev/gpiochip0");
	if (!chip) {
        return URJ_STATUS_OK;
    }

    line_cfg = gpiod_line_config_new();
	if (!line_cfg) {
		return URJ_STATUS_FAIL;
    }

    for (i = 0; i < GPIO_REQUIRED; i++)
    {
        struct gpiod_line_settings *settings = NULL;

        settings = gpiod_line_settings_new();
        if (!settings) {
            return URJ_STATUS_FAIL;
        }

        if (i == GPIO_TDO) {
            gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
        } else {
            gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
            gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
        }

        ret = gpiod_line_config_add_line_settings(line_cfg, &(p->jtag_gpios[i]), 1, settings);
        if (ret) {
            return URJ_STATUS_FAIL;
        }
    }

    rconfig = gpiod_request_config_new();
    if (!rconfig) {
        return URJ_STATUS_FAIL;
    }
    gpiod_request_config_set_consumer(rconfig, "urjtag");

    request = gpiod_chip_request_lines(chip, rconfig, line_cfg);
    if (!request) {
        return URJ_STATUS_FAIL;
    }

    p->request = request;

    return URJ_STATUS_OK;
}

static int
gpio_close (urj_cable_t *cable)
{
    int i;
    gpio_params_t *p = cable->params;
    gpiod_line_request_release(p->request);
    return URJ_STATUS_OK;
}

static void
gpio_help (urj_log_level_t ll, const char *cablename)
{
    urj_log (ll,
        _("Usage: cable %s tdi=<gpio_tdi> tdo=<gpio_tdo> "
        "tck=<gpio_tck> tms=<gpio_tms>\n"
        "\n"), cablename);
}

static int
gpio_connect (urj_cable_t *cable, const urj_param_t *params[])
{
    gpio_params_t *cable_params;
    int i;

    cable_params = calloc (1, sizeof (*cable_params));
    if (!cable_params)
    {
        urj_error_set (URJ_ERROR_OUT_OF_MEMORY, _("calloc(%zd) fails"),
                       sizeof (*cable_params));
        free (cable);
        return URJ_STATUS_FAIL;
    }

    cable_params->jtag_gpios[GPIO_TDI] = GPIO_UNSET;
    cable_params->jtag_gpios[GPIO_TDO] = GPIO_UNSET;
    cable_params->jtag_gpios[GPIO_TMS] = GPIO_UNSET;
    cable_params->jtag_gpios[GPIO_TCK] = GPIO_UNSET;
    if (params != NULL)
        /* parse arguments beyond the cable name */
        for (i = 0; params[i] != NULL; i++)
        {
            switch (params[i]->key)
            {
            case URJ_CABLE_PARAM_KEY_TDI:
                cable_params->jtag_gpios[GPIO_TDI] = params[i]->value.lu;
                break;
            case URJ_CABLE_PARAM_KEY_TDO:
                cable_params->jtag_gpios[GPIO_TDO] = params[i]->value.lu;
                break;
            case URJ_CABLE_PARAM_KEY_TMS:
                cable_params->jtag_gpios[GPIO_TMS] = params[i]->value.lu;
                break;
            case URJ_CABLE_PARAM_KEY_TCK:
                cable_params->jtag_gpios[GPIO_TCK] = params[i]->value.lu;
                break;
            default:
                break;
            }
        }

    urj_log (URJ_LOG_LEVEL_NORMAL,
        _("Initializing GPIO JTAG Chain\n"));

    /*
     * We need to configure the cable only once. Next time
     * is called, the old parameters are taken if a newer
     * is not passed
     */

    for (i = GPIO_TDI; i <= GPIO_TDO; i++)
        if (cable_params->jtag_gpios[i] == GPIO_UNSET)
        {
            urj_error_set (URJ_ERROR_SYNTAX, _("missing required gpios\n"));
            gpio_help (URJ_ERROR_SYNTAX, "gpio");
            return URJ_STATUS_FAIL;
        }

    cable->params = cable_params;
    cable->chain = NULL;
    cable->delay = 1000;

    return URJ_STATUS_OK;
}

static void
gpio_disconnect (urj_cable_t *cable)
{
    urj_tap_chain_disconnect (cable->chain);
    gpio_close (cable);
}

static void
gpio_cable_free (urj_cable_t *cable)
{
    free (cable->params);
    free (cable);
}

static int
gpio_init (urj_cable_t *cable)
{
    gpio_params_t *p = cable->params;

    if (gpio_open (cable) != URJ_STATUS_OK)
        return URJ_STATUS_FAIL;

    p->signals = URJ_POD_CS_TRST;

    return URJ_STATUS_OK;
}

static void
gpio_done (urj_cable_t *cable)
{
    gpio_close (cable);
}

static void
gpio_clock (urj_cable_t *cable, int tms, int tdi, int n)
{
    gpio_params_t *p = cable->params;
    int i;

    tms = tms ? 1 : 0;
    tdi = tdi ? 1 : 0;

    gpio_set_value (cable, p->jtag_gpios[GPIO_TMS], tms);
    gpio_set_value (cable, p->jtag_gpios[GPIO_TDI], tdi);

    for (i = 0; i < n; i++)
    {
        gpio_set_value (cable, p->jtag_gpios[GPIO_TCK], 0);
        gpio_set_value (cable, p->jtag_gpios[GPIO_TCK], 1);
        gpio_set_value (cable, p->jtag_gpios[GPIO_TCK], 0);
    }
}

static int
gpio_get_tdo ( urj_cable_t *cable )
{
    gpio_params_t *p = cable->params;

    gpio_set_value(cable, p->jtag_gpios[GPIO_TCK], 0);
    gpio_set_value(cable, p->jtag_gpios[GPIO_TDI], 0);
    gpio_set_value(cable, p->jtag_gpios[GPIO_TMS], 0);
    p->lastout &= ~(URJ_POD_CS_TMS | URJ_POD_CS_TDI | URJ_POD_CS_TCK);

    urj_tap_cable_wait (cable);

    return gpio_get_value (cable, p->jtag_gpios[GPIO_TDO]);
}

static int
gpio_current_signals (urj_cable_t *cable)
{
    gpio_params_t *p = cable->params;

    int sigs = p->signals & ~(URJ_POD_CS_TMS | URJ_POD_CS_TDI | URJ_POD_CS_TCK);

    if (p->lastout & URJ_POD_CS_TCK) sigs |= URJ_POD_CS_TCK;
    if (p->lastout & URJ_POD_CS_TDI) sigs |= URJ_POD_CS_TDI;
    if (p->lastout & URJ_POD_CS_TMS) sigs |= URJ_POD_CS_TMS;

    return sigs;
}

static int
gpio_set_signal (urj_cable_t *cable, int mask, int val)
{
    int prev_sigs = gpio_current_signals (cable);
    gpio_params_t *p = cable->params;

    mask &= (URJ_POD_CS_TDI | URJ_POD_CS_TCK | URJ_POD_CS_TMS); // only these can be modified

    if (mask != 0)
    {
        if (mask & URJ_POD_CS_TMS)
            gpio_set_value (cable, p->jtag_gpios[GPIO_TMS], val & URJ_POD_CS_TMS);
        if (mask & URJ_POD_CS_TDI)
            gpio_set_value (cable, p->jtag_gpios[GPIO_TDI], val & URJ_POD_CS_TDI);
        if (mask & URJ_POD_CS_TCK)
            gpio_set_value (cable, p->jtag_gpios[GPIO_TCK], val & URJ_POD_CS_TCK);
    }

    p->lastout = val & mask;

    return prev_sigs;
}

static int
gpio_get_signal (urj_cable_t *cable, urj_pod_sigsel_t sig)
{
    return (gpio_current_signals (cable) & sig) ? 1 : 0;
}

const urj_cable_driver_t urj_tap_cable_gpio_driver = {
    "gpio",
    N_("GPIO JTAG Chain"),
    URJ_CABLE_DEVICE_OTHER,
    { .other = gpio_connect, },
    gpio_disconnect,
    gpio_cable_free,
    gpio_init,
    gpio_done,
    urj_tap_cable_generic_set_frequency,
    gpio_clock,
    gpio_get_tdo,
    urj_tap_cable_generic_transfer,
    gpio_set_signal,
    gpio_get_signal,
    urj_tap_cable_generic_flush_one_by_one,
    gpio_help
};
