/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id: power-nano2g.c 28190 2010-10-01 18:09:10Z Buschel $
 *
 * Copyright © 2009 Bertrik Sikken
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include <stdbool.h>
#include "config.h"
#include "inttypes.h"
#include "s5l87xx.h"
#include "power.h"
#include "panic.h"
#include "pmu-target.h"
#include "usb_core.h"   /* for usb_charging_maxcurrent_change */
#include "backlight.h"
#include "timeout.h"

static int idepowered;

void power_off(void)
{
    /* USB inserted or EXTON1 */
    pmu_set_wake_condition(
            PCF5063X_OOCWAKE_EXTON2 | PCF5063X_OOCWAKE_EXTON1);
    pmu_enter_standby();

    while(1);
}

void power_init(void)
{
    pmu_init();

    idepowered = false;

    /* DOWN1CTL: CPU DVM step time = 30us (default: no DVM) */
    pmu_write(0x20, 2);

    /* USB power configuration:
     *
     * GPIO C0 is probably related to the LTC4066's CLPROG
     * pin (see datasheet). Setting it high allows to double
     * the maximum current selected by HPWR:
     *
     *  GPIO B6     GPIO C0     USB current
     *  HPWR        CLPROG ???  limit (mA)
     *  -------     ----------  -----------
     *  0           0           100
     *  1           0           500
     *  0           1           200
     *  1           1           1000 ??? (max.seen ~750mA)
     *
     * USB current limit includes battery charge and device
     * consumption. Battery charge has it's own limit at
     * 330~340 mA (configured using RPROG).
     *
     * Setting either of GPIO C1 or GPIO C2 disables battery
     * charge, power needed for device consumptiom is drained
     * from USB or AC adaptor when present. If external power
     * is not present or it is insufficient or limited,
     * additional required power is drained from battery.
     */
    PCON11 = (PCON11 & 0x000000ff)
          | (0xe << 8)      /* route D+ to ADC2: off */
          | (0xe << 12)     /* route D- to ADC2: off */
          | (0x0 << 16)     /* USB related input, POL pin ??? */
          | (0x0 << 20)     /* USB related input, !CHRG pin ??? */
          | (0xe << 24)     /* HPWR: 100mA */
          | (0xe << 28);    /* USB suspend: off */

    PCON12 = (PCON12 & 0xffff0000)
          | (0xe << 0)      /* double HPWR limit: off */
          | (0xe << 4)      /* disable battery charge: off */
          | (0xe << 8)      /* disable battery charge: off */
          | (0x0 << 12);    /* USB inserted/not inserted */
}

void ide_power_enable(bool on)
{
    idepowered = on;
    pmu_hdd_power(on);
}

bool ide_powered()
{
    return idepowered;
}

#if CONFIG_CHARGING

#ifdef HAVE_USB_CHARGING_ENABLE
void usb_charging_maxcurrent_change(int maxcurrent)
{
    bool suspend_charging = (maxcurrent < 100);
    bool fast_charging = (maxcurrent >= 500);

    /* This GPIO is connected to the LTC4066's SUSP pin */
    /* Setting it high prevents any power being drawn over USB */
    /* which supports USB suspend */
    GPIOCMD = 0xb070e | (suspend_charging ? 1 : 0);

    /* This GPIO is connected to the LTC4066's HPWR pin */
    /* Setting it low limits current to 100mA, setting it high allows 500mA */
    GPIOCMD = 0xb060e | (fast_charging ? 1 : 0);

    /* GPIO C1: disable battery charging when USB current commitment
     * is insufficient (< 500mA).  This prevents charge oscillation
     * when connected to MFi DACs without power bank, where the
     * source can't deliver enough for device + charge current.
     * Device still draws operating power from USB; battery only
     * supplements if USB is insufficient. */
    GPIOCMD = 0xc010e | (fast_charging ? 0 : 1);
}
#endif

static struct timeout chrg_monitor_tmo;
static volatile bool chrg_saw_discharge;

/* Runs from tick ISR every 10ms.  Catches brief !CHRG HIGH
 * pulses that the 500ms power thread polling misses.  At high
 * battery (>80%), the LTC4066 sustains low charge current from
 * weak USB sources but briefly drops out every 5-10s for <500ms. */
static int chrg_monitor_cb(struct timeout *tmo)
{
    (void)tmo;
    if (PDAT(11) & 0x10)   /* !CHRG HIGH = not charging */
        chrg_saw_discharge = true;
    return 1;  /* re-run next tick (10ms) */
}

unsigned int power_input_status(void)
{
    static bool usb_charger_detected;
    static bool prev_bl_on;
    static bool monitoring;
    static int debounce;
    unsigned int status = POWER_INPUT_NONE;
    if (usb_detect() == USB_INSERTED)
    {
        status |= POWER_INPUT_USB;
        bool bl_on = is_backlight_on(true);
        if (bl_on && !prev_bl_on)
        {
            /* BL just turned on: probe C1, start monitor */
            GPIOCMD = 0xc010e | 0;  /* C1 LOW */
            chrg_saw_discharge = false;
            debounce = 0;
            if (!monitoring)
            {
                timeout_register(&chrg_monitor_tmo, chrg_monitor_cb,
                                 1, 0);
                monitoring = true;
            }
        }
        else if (bl_on)
        {
            /* Check high-frequency monitor flag */
            if (chrg_saw_discharge)
            {
                chrg_saw_discharge = false;
                usb_charger_detected = false;
                debounce = 0;
            }
            else if (!usb_charger_detected)
            {
                if (++debounce >= 8)
                {
                    usb_charger_detected = true;
                    debounce = 0;
                }
            }
            /* For charger removal (PB toggled off): only count
             * false readings.  Don't reset on true — oscillation
             * (~50/50) must accumulate false readings to clear. */
            else if (!charging_state())
            {
                if (++debounce >= 8)
                {
                    usb_charger_detected = false;
                    debounce = 0;
                }
            }
            /* true readings: don't reset — oscillation must
             * be able to clear via accumulated false readings. */
        }
        else
        {
            /* BL off: stop monitor, control C1 */
            if (monitoring)
            {
                timeout_cancel(&chrg_monitor_tmo);
                monitoring = false;
            }
            if (!usb_charger_detected)
                GPIOCMD = 0xc010e | 1;  /* C1 HIGH: block */
        }
        /* BL off + charger detected: C1 stays LOW, charging
         * continues from the real charger source. */
        if (usb_charger_detected)
            status |= POWER_INPUT_USB_CHARGER;
        prev_bl_on = bl_on;
    }
    else
    {
        if (monitoring)
        {
            timeout_cancel(&chrg_monitor_tmo);
            monitoring = false;
        }
        usb_charger_detected = false;
        prev_bl_on = false;
        debounce = 0;
    }
    if (pmu_firewire_present())
        status |= POWER_INPUT_MAIN_CHARGER;
    return status;
}

bool charging_state(void)
{
    return (PDAT(11) & 0x10) ? 0 : 1;
}
#endif /* CONFIG_CHARGING */
