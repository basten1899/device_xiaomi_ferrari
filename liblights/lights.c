/*
 * Copyright (C) 2014-2015 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


//#define LOG_NDEBUG 0
#define LOG_TAG "lights"

#include <cutils/log.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/types.h>

#include <hardware/lights.h>

/******************************************************************************/

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct light_state_t g_attention;
static struct light_state_t g_notification;
static struct light_state_t g_battery;

const char *const RED_LED_FILE
        = "/sys/class/leds/red/brightness";

const char *const RED_BLINK_FILE
        = "/sys/class/leds/red/blink";

const char *const RED_RISETIME_FILE
        = "/sys/class/leds/red/risetime";

const char *const RED_FALLTIME_FILE
        = "/sys/class/leds/red/falltime";

const char *const GREEN_LED_FILE
        = "/sys/class/leds/green/brightness";

const char *const GREEN_BLINK_FILE
        = "/sys/class/leds/green/blink";

const char *const GREEN_RISETIME_FILE
        = "/sys/class/leds/green/risetime";

const char *const GREEN_FALLTIME_FILE
        = "/sys/class/leds/green/falltime";

const char *const BLUE_LED_FILE
        = "/sys/class/leds/blue/brightness";

const char *const BLUE_BLINK_FILE
        = "/sys/class/leds/blue/blink";

const char *const BLUE_RISETIME_FILE
        = "/sys/class/leds/blue/risetime";

const char *const BLUE_FALLTIME_FILE
        = "/sys/class/leds/blue/falltime";

const char *const LCD_FILE
        = "/sys/class/leds/lcd-backlight/brightness";

const char *const BUTTONS_FILE
        = "/sys/class/leds/button-backlight/brightness";

/**
 * device methods
 */

void init_globals(void)
{
    // init the mutex
    pthread_mutex_init(&g_lock, NULL);
}

static int
write_int(char const* path, int value)
{
    int fd;
    static int already_warned = 0;

    fd = open(path, O_RDWR);
    if (fd >= 0) {
        char buffer[20];
        int bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
        ssize_t amt = write(fd, buffer, (size_t)bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

static int
is_lit(struct light_state_t const* state)
{
    return state->color & 0x00ffffff;
}

static int
rgb_to_brightness(const struct light_state_t *state)
{
    int color = state->color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff))
            + (150 * ((color >> 8) & 0xff))
            + (29 * (color & 0xff))) >> 8;
}

static int
convert_ms(int ms)
{
    int ms_out = 3;
    if (ms <= 250) {
        ms_out = 1;
    } else if (ms <=500 ) {
        ms_out = 2;
    } else if (ms <=1000) {
        ms_out = 3;
    } else if (ms <=2500) {
        ms_out = 4;
    } else if (ms <=5000) {
        ms_out = 5;
    }
    return ms_out;
}

void reset_leds(void)
{
    write_int(RED_LED_FILE, 0);
    write_int(GREEN_LED_FILE, 0);
    write_int(BLUE_LED_FILE, 0);
    write_int(RED_BLINK_FILE, 0);
    write_int(GREEN_BLINK_FILE, 0);
    write_int(BLUE_BLINK_FILE, 0);
    write_int(RED_RISETIME_FILE, 3);
    write_int(GREEN_RISETIME_FILE, 3);
    write_int(BLUE_RISETIME_FILE, 3);
    write_int(RED_FALLTIME_FILE, 3);
    write_int(GREEN_FALLTIME_FILE, 3);
    write_int(BLUE_FALLTIME_FILE, 3);
}

static int
set_battery_light_locked(struct light_device_t *dev,
        struct light_state_t const* state)
{
    int red, green, blue;
    unsigned int colorRGB;

    reset_leds();

    colorRGB = state->color;

    red = (colorRGB >> 16) & 0xFF;
    green = (colorRGB >> 8) & 0xFF;
    blue = colorRGB & 0xFF;

    write_int(RED_LED_FILE, red);
    write_int(GREEN_LED_FILE, green);
    write_int(BLUE_LED_FILE, blue);

    return 0;
}

static int
set_speaker_light_locked(struct light_device_t *dev,
        struct light_state_t const* state)
{
    int red, green, blue;
    int blink, blinktime = 3;

    unsigned int colorRGB;

    if(!dev) {
        return -1;
    }

    reset_leds();

    if (state == NULL) {
        return 0;
    }

/*
 Something wrong with light_state_t, becaulse state->flashMode is definetely not flashmode
 looks state->flasmode is device locked or not & FlashOnMS is 1 if always on & 500/1000/etc when flash mode is set
 */

    if (state->flashOnMS  != 1) {
        blink = 1;
        blinktime = convert_ms(state->flashOnMS);
    } else {
        blink = 0;
    }

    colorRGB = state->color;

    red = (colorRGB >> 16) & 0xFF;
    green = (colorRGB >> 8) & 0xFF;
    blue = colorRGB & 0xFF;

    ALOGD("set_speaker_light_locked mode %d, colorRGB=%08X, red=%d, green=%d, blue=%d on=%d off=%d\n",
            state->flashMode, colorRGB, red, green, blue, state->flashOnMS, state->flashOffMS);

    if (blink) {
        if (red) {
            write_int(RED_LED_FILE, 0);
            write_int(RED_BLINK_FILE, blink);
            write_int(RED_RISETIME_FILE, blinktime);
            write_int(RED_FALLTIME_FILE, blinktime);
        }
        if (green) {
            write_int(GREEN_LED_FILE, 0);
            write_int(GREEN_BLINK_FILE, blink);
            write_int(GREEN_RISETIME_FILE, blinktime);
            write_int(GREEN_FALLTIME_FILE, blinktime);
        }
        if (blue) {
            write_int(BLUE_LED_FILE, 0);
            write_int(BLUE_BLINK_FILE, blink);
            write_int(BLUE_RISETIME_FILE, blinktime);
            write_int(BLUE_FALLTIME_FILE, blinktime);
       }
    } else {
        write_int(RED_LED_FILE, red);
        write_int(GREEN_LED_FILE, green);
        write_int(BLUE_LED_FILE, blue);
    }

    return 0;
}

static void
handle_speaker_light_locked(struct light_device_t *dev)
{
    set_speaker_light_locked(dev, NULL);
    if (is_lit(&g_attention)) {
        set_speaker_light_locked(dev, &g_attention);
    } else if (is_lit(&g_notification)) {
        set_speaker_light_locked(dev, &g_notification);
    } else {
        set_battery_light_locked(dev, &g_battery);
    }
}

static int
set_light_backlight(struct light_device_t *dev,
        const struct light_state_t *state)
{
    int err = 0;
    int brightness = rgb_to_brightness(state);

    pthread_mutex_lock(&g_lock);

    err = write_int(LCD_FILE, brightness);

    pthread_mutex_unlock(&g_lock);

    return err;
}

static int
set_light_buttons(struct light_device_t *dev,
        const struct light_state_t *state)
{
    int err = 0;
    int brightness = rgb_to_brightness(state);

    pthread_mutex_lock(&g_lock);

    err = write_int(BUTTONS_FILE, brightness);

    pthread_mutex_unlock(&g_lock);

    return err;
}

static int
set_light_attention(struct light_device_t *dev,
        const struct light_state_t *state)
{
    pthread_mutex_lock(&g_lock);

    g_attention = *state;
    handle_speaker_light_locked(dev);

    pthread_mutex_unlock(&g_lock);

    return 0;
}

static int
set_light_notifications(struct light_device_t *dev,
        const struct light_state_t *state)
{
    pthread_mutex_lock(&g_lock);

    g_notification = *state;
    handle_speaker_light_locked(dev);

    pthread_mutex_unlock(&g_lock);

    return 0;
}

static int
set_light_battery(struct light_device_t *dev,
        const struct light_state_t *state)
{
    pthread_mutex_lock(&g_lock);

    g_battery = *state;
    handle_speaker_light_locked(dev);

    pthread_mutex_unlock(&g_lock);

    return 0;
}

/** Close the lights device */
static int
close_lights(struct light_device_t *dev)
{
    if (dev) {
        free(dev);
    }
    return 0;
}

/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t *module, const char *name,
        struct hw_device_t **device)
{
    int (*set_light)(struct light_device_t *dev,
            const struct light_state_t *state);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name))
        set_light = set_light_backlight;
    else if (0 == strcmp(LIGHT_ID_BUTTONS, name))
        set_light = set_light_buttons;
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name))
        set_light = set_light_notifications;
    else if (0 == strcmp(LIGHT_ID_ATTENTION, name))
        set_light = set_light_attention;
    else if (0 == strcmp(LIGHT_ID_BATTERY, name))
        set_light = set_light_battery;
    else
        return -EINVAL;

    pthread_once(&g_init, init_globals);

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));
    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = (int (*)(struct hw_device_t*))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

/*
 * The lights Module
 */
struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "Lights Module",
    .author = "The CyanogenMod Project",
    .methods = &lights_module_methods,
};
