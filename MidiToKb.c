/*
 * ORIGINAL COPYRIGHT:
 * -------------------------------------------------------------------------------
 *  MidiToKb.c
 *
 * ORIGINAL COPYRIGHT (original application)
 *  Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 * MODIFIED COPYRIGHT (derived application)
 *  Copyright (c) Nate Simon
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * -------------------------------------------------------------------------------
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <alsa/asoundlib.h>
#include <linux/uinput.h>

#define CC_ASSERT(cond) int __constraint_violated[cond] = {0}

#define NSEC_PER_SEC 1000000000L

#define MIDI_TO_KB_VERSION_STR "1.0"

static int do_device_list, do_rawmidi_list;
static char *port_name = "";
static char *send_data;
static int send_data_length;
static float timeout;
static int stop;
static int sysex_interval;
static snd_rawmidi_t *input, **inputp;
static snd_rawmidi_t *output, **outputp;


#define ARRAY_LENGTH(a) (sizeof(a)/sizeof(a[0]))

struct supported_keys_t {
    const char *ascii;
    int keyCode;
};

const struct supported_keys_t SUPPORTED_KEYS_ARRAY[] = {
//  {ascii,         keyCode         },
    {"A",           KEY_A           },
    {"B",           KEY_B           },
    {"C",           KEY_C           },
    {"D",           KEY_D           },
    {"E",           KEY_E           },
    {"F",           KEY_F           },
    {"G",           KEY_G           },
    {"H",           KEY_H           },
    {"I",           KEY_I           },
    {"J",           KEY_J           },
    {"K",           KEY_K           },
    {"L",           KEY_L           },
    {"M",           KEY_M           },
    {"N",           KEY_N           },
    {"O",           KEY_O           },
    {"P",           KEY_P           },
    {"Q",           KEY_Q           },
    {"R",           KEY_R           },
    {"S",           KEY_S           },
    {"T",           KEY_T           },
    {"U",           KEY_U           },
    {"V",           KEY_V           },
    {"W",           KEY_W           },
    {"X",           KEY_X           },
    {"Y",           KEY_Y           },
    {"Z",           KEY_Z           },
    {"1",           KEY_1           },
    {"2",           KEY_2           },
    {"3",           KEY_3           },
    {"4",           KEY_4           },
    {"5",           KEY_5           },
    {"6",           KEY_6           },
    {"7",           KEY_7           },
    {"8",           KEY_8           },
    {"9",           KEY_9           },
    {"0",           KEY_0           },
    {"F1",          KEY_F1          },
    {"F2",          KEY_F2          },
    {"F3",          KEY_F3          },
    {"F4",          KEY_F4          },
    {"F5",          KEY_F5          },
    {"F6",          KEY_F6          },
    {"F7",          KEY_F7          },
    {"F8",          KEY_F8          },
    {"F9",          KEY_F9          },
    {"F10",         KEY_F10         },
    {"F11",         KEY_F11         },
    {"F12",         KEY_F12         },
    {"ESC",         KEY_ESC         },
    {"ALT",         KEY_LEFTALT     },
    {"CTRL",        KEY_LEFTCTRL    },
    {"SHIFT",       KEY_LEFTSHIFT   },
    {"BACKSPACE",   KEY_BACKSPACE   },
    {" ",           KEY_SPACE       },
    {"SPACE",       KEY_SPACE       },
    {"PG_UP",       KEY_PAGEUP      },
    {"PG_DOWN",     KEY_PAGEDOWN    },
    {"UP",          KEY_UP          },
    {"DOWN",        KEY_DOWN        },
    {"LEFT",        KEY_LEFT        },
    {"RIGHT",       KEY_RIGHT       },
    {"DEL",         KEY_DELETE      },
    {"RETURN",      KEY_ENTER       },
    {"MINUS",       KEY_MINUS       },
    {"EQUAL",       KEY_EQUAL       },
    {"HOME",        KEY_HOME        },
};
#define KEY_COUNT (ARRAY_LENGTH(SUPPORTED_KEYS_ARRAY))

typedef struct KeymapNodeT
{
    struct KeymapNodeT *pNext;
    unsigned char key;
    char *action;
} KEYMAP_NODE_T;
KEYMAP_NODE_T *gKeymapRoot = NULL;


static void error(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    putc('\n', stderr);
}


static void usage(void)
{
    printf(
        "Usage: miditokb options\n"
        "\n"
        "-h, --help                     this help\n"
        "-v --verbose                   enable verbosity\n"
        "-V, --version                  print current version\n"
        "-k, --keymap                   keymap file\n"
        "-l, --list-devices             list all hardware ports\n"
        "-L, --list-rawmidis            list all RawMIDI definitions\n"
        "-p, --port=name                select port by name\n"
        "-t, --timeout=seconds          exits when no data has been received\n"
        "                               for the specified duration\n"
        "-a, --active-sensing           include active sensing bytes\n"
        "-c, --clock                    include clock bytes\n"
        "-i, --sysex-interval=mseconds  delay in between each SysEx message\n");
}


static void version(void)
{
    puts("miditokb version " MIDI_TO_KB_VERSION_STR);
}


static int load_keymap(char *keymap_file)
{
    FILE *km_file = fopen(keymap_file, "r");
    if (km_file == NULL)
    {
        printf("Failed to open %s for reading!\n", keymap_file);
        return -1;
    }
    char line[160];
    unsigned char midi_key;
    char *action;

    KEYMAP_NODE_T **pNextNodePtr = &gKeymapRoot;

    while (fgets(line, sizeof(line), km_file) != NULL)
    {
        if (line[strlen(line)-1] == '\n')
        {
            line[strlen(line)-1] = '\x00';
        }
        if (line[0] == '#')
        {
            continue;
        }
        midi_key = strtol(strtok(line, ","), NULL, 0);
        action = strtok(NULL, ",");
        if (midi_key == 0 || action == NULL)
        {
            continue;
        }

        KEYMAP_NODE_T *nextNode = (KEYMAP_NODE_T*)calloc(1, sizeof(KEYMAP_NODE_T));
        nextNode->key = midi_key;
        nextNode->action = (char*)calloc(1, strlen(action));
        strcpy(nextNode->action, action);

        *pNextNodePtr = nextNode;
        pNextNodePtr = &nextNode->pNext;

        printf("Loaded key=%#x, action=%s\n", midi_key, action);
    }
    fclose(km_file);

    return 0;
}

static const char* keymap_get_action(unsigned char key)
{
    if (gKeymapRoot == NULL)
    {
        // No keymap loaded
        return NULL;
    }

    const char *action = NULL;
    KEYMAP_NODE_T *currentNode = gKeymapRoot;
    do
    {
        if (currentNode->key == key)
        {
            action = currentNode->action;
        }
    } while ((currentNode = currentNode->pNext) && !action);

    return action;
}


static int initialize_kb(void)
{
    int kbFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (kbFd == -1)
    {
        return kbFd;
    }

    ioctl(kbFd, UI_SET_EVBIT, EV_KEY);
    for (int keyIdx; keyIdx < KEY_COUNT; keyIdx++)
    {
        ioctl(kbFd, UI_SET_KEYBIT, SUPPORTED_KEYS_ARRAY[keyIdx].keyCode);
    }

    struct uinput_setup usetup = {0};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "MIDI virtual keyboard device");
    ioctl(kbFd, UI_DEV_SETUP, &usetup);
    ioctl(kbFd, UI_DEV_CREATE);

    return kbFd;
}


static void close_kb(int kbFd)
{
    ioctl(kbFd, UI_DEV_DESTROY);
    close(kbFd);
}


static int str_key_to_event(char *key)
{
    int result = -1;
    for (int keyIdx = 0; keyIdx < KEY_COUNT; keyIdx++)
    {
        if (strcmp(SUPPORTED_KEYS_ARRAY[keyIdx].ascii, key) == 0)
        {
            result = SUPPORTED_KEYS_ARRAY[keyIdx].keyCode;
            break;
        }
    }
    printf("%s=%#x; ", key, result);
    return result;
}


static void emit_key(int kbFd, char *keys, int keyCnt)
{
    struct input_event reportEvt = {0};
    reportEvt.type = EV_SYN;
    reportEvt.code = SYN_REPORT;
    reportEvt.value = 0;
    reportEvt.time.tv_sec = 0;
    reportEvt.time.tv_usec = 0;

    for (int emitValue = 1; emitValue >= 0; emitValue--)
    {
        for (int inputKeyIdx = 0; inputKeyIdx < keyCnt; inputKeyIdx++)
        {
            struct input_event keyEvt = {0};
            keyEvt.type = EV_KEY;
            keyEvt.code = keys[inputKeyIdx];
            keyEvt.value = emitValue;
            keyEvt.time.tv_sec = 0;
            keyEvt.time.tv_usec = 0;
            write(kbFd, &keyEvt, sizeof(keyEvt));
        }
        write(kbFd, &reportEvt, sizeof(reportEvt));

    }
}

static void perform_action(int kbFd, const char *action)
{
    char *actionCpy = (char*)malloc(strlen(action));
    strcpy(actionCpy, action);
    char events[10] = {0};
    int event_cnt = 0;
    char *next_key = strtok(actionCpy, "+");
    printf("Tokens: ");
    while (next_key != NULL)
    {
        char next_evt = str_key_to_event(next_key);

        if (next_evt != -1)
        {
            events[event_cnt++] = next_evt;
        }
        next_key = strtok(NULL, "+");
    }
    printf("\n");

    emit_key(kbFd, events, event_cnt);
}


static void list_device(snd_ctl_t *ctl, int card, int device)
{
    snd_rawmidi_info_t *info;
    const char *name;
    const char *sub_name;
    int subs, subs_in, subs_out;
    int sub;
    int err;

    snd_rawmidi_info_alloca(&info);
    snd_rawmidi_info_set_device(info, device);

    snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
    err = snd_ctl_rawmidi_info(ctl, info);
    if (err >= 0)
        subs_in = snd_rawmidi_info_get_subdevices_count(info);
    else
        subs_in = 0;

    snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
    err = snd_ctl_rawmidi_info(ctl, info);
    if (err >= 0)
        subs_out = snd_rawmidi_info_get_subdevices_count(info);
    else
        subs_out = 0;

    subs = subs_in > subs_out ? subs_in : subs_out;
    if (!subs)
        return;

    for (sub = 0; sub < subs; ++sub) {
        snd_rawmidi_info_set_stream(info, sub < subs_in ?
                        SND_RAWMIDI_STREAM_INPUT :
                        SND_RAWMIDI_STREAM_OUTPUT);
        snd_rawmidi_info_set_subdevice(info, sub);
        err = snd_ctl_rawmidi_info(ctl, info);
        if (err < 0) {
            error("cannot get rawmidi information %d:%d:%d: %s\n",
                  card, device, sub, snd_strerror(err));
            return;
        }
        name = snd_rawmidi_info_get_name(info);
        sub_name = snd_rawmidi_info_get_subdevice_name(info);
        if (sub == 0 && sub_name[0] == '\0') {
            printf("%c%c  hw:%d,%d    %s",
                   sub < subs_in ? 'I' : ' ',
                   sub < subs_out ? 'O' : ' ',
                   card, device, name);
            if (subs > 1)
                printf(" (%d subdevices)", subs);
            putchar('\n');
            break;
        } else {
            printf("%c%c  hw:%d,%d,%d  %s\n",
                   sub < subs_in ? 'I' : ' ',
                   sub < subs_out ? 'O' : ' ',
                   card, device, sub, sub_name);
        }
    }
}


static void list_card_devices(int card)
{
    snd_ctl_t *ctl;
    char name[32];
    int device;
    int err;

    sprintf(name, "hw:%d", card);
    if ((err = snd_ctl_open(&ctl, name, 0)) < 0) {
        error("cannot open control for card %d: %s", card, snd_strerror(err));
        return;
    }
    device = -1;
    for (;;) {
        if ((err = snd_ctl_rawmidi_next_device(ctl, &device)) < 0) {
            error("cannot determine device number: %s", snd_strerror(err));
            break;
        }
        if (device < 0)
            break;
        list_device(ctl, card, device);
    }
    snd_ctl_close(ctl);
}


static void device_list(void)
{
    int card, err;

    card = -1;
    if ((err = snd_card_next(&card)) < 0) {
        error("cannot determine card number: %s", snd_strerror(err));
        return;
    }
    if (card < 0) {
        error("no sound card found");
        return;
    }
    puts("Dir Device    Name");
    do {
        list_card_devices(card);
        if ((err = snd_card_next(&card)) < 0) {
            error("cannot determine card number: %s", snd_strerror(err));
            break;
        }
    } while (card >= 0);
}


static void rawmidi_list(void)
{
    snd_output_t *output;
    snd_config_t *config;
    int err;

    if ((err = snd_config_update()) < 0) {
        error("snd_config_update failed: %s", snd_strerror(err));
        return;
    }
    if ((err = snd_output_stdio_attach(&output, stdout, 0)) < 0) {
        error("snd_output_stdio_attach failed: %s", snd_strerror(err));
        return;
    }
    if (snd_config_search(snd_config, "rawmidi", &config) >= 0) {
        puts("RawMIDI list:");
        snd_config_save(config, output);
    }
    snd_output_close(output);
}


static int send_midi_interleaved(void)
{
    int err;
    char *data = send_data;
    size_t buffer_size;
    snd_rawmidi_params_t *param;
    snd_rawmidi_status_t *st;

    snd_rawmidi_status_alloca(&st);

    snd_rawmidi_params_alloca(&param);
    snd_rawmidi_params_current(output, param);
    buffer_size = snd_rawmidi_params_get_buffer_size(param);

    while (data < (send_data + send_data_length)) {
        int len = send_data + send_data_length - data;
        char *temp;

        if (data > send_data) {
            snd_rawmidi_status(output, st);
            do {
                /* 320 µs per byte as noted in Page 1 of MIDI spec */
                usleep((buffer_size - snd_rawmidi_status_get_avail(st)) * 320);
                snd_rawmidi_status(output, st);
            } while(snd_rawmidi_status_get_avail(st) < buffer_size);
            usleep(sysex_interval * 1000);
        }

        /* find end of SysEx */
        if ((temp = memchr(data, 0xf7, len)) != NULL)
            len = temp - data + 1;

        if ((err = snd_rawmidi_write(output, data, len)) < 0)
            return err;

        data += len;
    }

    return 0;
}


/*
 * prints MIDI commands, formatting them nicely
 */
static void print_byte(unsigned char byte)
{
    static enum {
        STATE_UNKNOWN,
        STATE_1PARAM,
        STATE_1PARAM_CONTINUE,
        STATE_2PARAM_1,
        STATE_2PARAM_2,
        STATE_2PARAM_1_CONTINUE,
        STATE_SYSEX
    } state = STATE_UNKNOWN;
    int newline = 0;

    if (byte >= 0xf8)
        newline = 1;
    else if (byte >= 0xf0) {
        newline = 1;
        switch (byte) {
        case 0xf0:
            state = STATE_SYSEX;
            break;
        case 0xf1:
        case 0xf3:
            state = STATE_1PARAM;
            break;
        case 0xf2:
            state = STATE_2PARAM_1;
            break;
        case 0xf4:
        case 0xf5:
        case 0xf6:
            state = STATE_UNKNOWN;
            break;
        case 0xf7:
            newline = state != STATE_SYSEX;
            state = STATE_UNKNOWN;
            break;
        }
    } else if (byte >= 0x80) {
        newline = 1;
        if (byte >= 0xc0 && byte <= 0xdf)
            state = STATE_1PARAM;
        else
            state = STATE_2PARAM_1;
    } else /* b < 0x80 */ {
        int running_status = 0;
        newline = state == STATE_UNKNOWN;
        switch (state) {
        case STATE_1PARAM:
            state = STATE_1PARAM_CONTINUE;
            break;
        case STATE_1PARAM_CONTINUE:
            running_status = 1;
            break;
        case STATE_2PARAM_1:
            state = STATE_2PARAM_2;
            break;
        case STATE_2PARAM_2:
            state = STATE_2PARAM_1_CONTINUE;
            break;
        case STATE_2PARAM_1_CONTINUE:
            running_status = 1;
            state = STATE_2PARAM_2;
            break;
        default:
            break;
        }
        if (running_status)
            fputs("\n  ", stdout);
    }
    printf("%c%02X", newline ? '\n' : ' ', byte);
}

static void parse_rx_data(int kbFd, unsigned char *buf, int bufLen)
{
    int currentIdx = 0;
    while (currentIdx < bufLen)
    {
        unsigned char data = buf[currentIdx++];
        unsigned char dataNext = buf[currentIdx];
        const char *action = keymap_get_action(data);
        if (data >= 0x80)  // Control characters
        {
            //
        }
        else if ((action != NULL) && (dataNext != 0))
        {
            printf("\nInput: %s\n", action);
            perform_action(kbFd, action);
            currentIdx++;
        }
    }
}


static void sig_handler(int dummy)
{
    stop = 1;
}


int main(int argc, char *argv[])
{
    static const char short_options[] = "hVk:lLp:t:aci:";
    static const struct option long_options[] = {
        {"help", 0, NULL, 'h'},
        {"version", 0, NULL, 'V'},
        {"keymap", 1, NULL, 'k'},
        {"list-devices", 0, NULL, 'l'},
        {"list-rawmidis", 0, NULL, 'L'},
        {"port", 1, NULL, 'p'},
        {"name", 1, NULL, 'n'},
        {"timeout", 1, NULL, 't'},
        {"active-sensing", 0, NULL, 'a'},
        {"clock", 0, NULL, 'c'},
        {"sysex-interval", 1, NULL, 'i'},
        { }
    };
    int c, err, ok = 0;
    int ignore_active_sensing = 1;
    int ignore_clock = 1;
    int kbFd = -1;
    char *keymap_file = "";
    struct itimerspec itimerspec = { .it_interval = { 0, 0 } };

    while ((c = getopt_long(argc, argv, short_options,
                     long_options, NULL)) != -1) {
        switch (c) {
        case 'h':
            usage();
            return 0;
        case 'V':
            version();
            return 0;
        case 'k':
            keymap_file = optarg;
            break;
        case 'l':
            do_device_list = 1;
            break;
        case 'L':
            do_rawmidi_list = 1;
            break;
        case 'p':
            port_name = optarg;
            break;
        case 't':
            if (optarg)
                timeout = atof(optarg);
            break;
        case 'a':
            ignore_active_sensing = 0;
            break;
        case 'c':
            ignore_clock = 0;
            break;
        case 'i':
            sysex_interval = atoi(optarg);
            break;
        default:
            error("Try `amidi --help' for more information.");
            return 1;
        }
    }
    if (argv[optind])
    {
        error("%s is not an option.", argv[optind]);
        return 1;
    }

    if (do_rawmidi_list)
    {
        rawmidi_list();
    }
    if (do_device_list)
    {
        device_list();
    }
    if (do_rawmidi_list || do_device_list)
    {
        return 0;
    }

    if (strcmp(port_name, "") == 0)
    {
        error("port must be specified!");
        goto _exit2;
    }


    if (strcmp(keymap_file, "") != 0)
    {
        err = load_keymap(keymap_file);
        if (err)
        {
            error("Failed to load keymap, error code %d", err);
            goto _exit2;
        }
    }

    inputp = &input;

    outputp = NULL;

    if ((err = snd_rawmidi_open(inputp, outputp, port_name, SND_RAWMIDI_NONBLOCK)) < 0) {
        error("cannot open port \"%s\": %s", port_name, snd_strerror(err));
        goto _exit2;
    }

    if (inputp)
        snd_rawmidi_read(input, NULL, 0); /* trigger reading */

    if (send_data) {
        if ((err = snd_rawmidi_nonblock(output, 0)) < 0) {
            error("cannot set blocking mode: %s", snd_strerror(err));
            goto _exit;
        }
        if (!sysex_interval) {
            if ((err = snd_rawmidi_write(output, send_data, send_data_length)) < 0) {
                error("cannot send data: %s", snd_strerror(err));
                return err;
            }
        } else {
            if ((err = send_midi_interleaved()) < 0) {
                error("cannot send data: %s", snd_strerror(err));
                return err;
            }
        }
    }

    kbFd = initialize_kb();

    if (inputp) {
        int read = 0;
        int npfds;
        struct pollfd *pfds;

        npfds = 1 + snd_rawmidi_poll_descriptors_count(input);
        pfds = alloca(npfds * sizeof(struct pollfd));

        if (timeout > 0) {
            pfds[0].fd = timerfd_create(CLOCK_MONOTONIC, 0);
            if (pfds[0].fd == -1) {
                error("cannot create timer: %s", strerror(errno));
                goto _exit;
            }
            pfds[0].events = POLLIN;
        } else {
            pfds[0].fd = -1;
        }

        snd_rawmidi_poll_descriptors(input, &pfds[1], npfds - 1);

        signal(SIGINT, sig_handler);

        if (timeout > 0) {
            float timeout_int;

            itimerspec.it_value.tv_nsec = modff(timeout, &timeout_int) * NSEC_PER_SEC;
            itimerspec.it_value.tv_sec = timeout_int;
            err = timerfd_settime(pfds[0].fd, 0, &itimerspec, NULL);
            if (err < 0) {
                error("cannot set timer: %s", strerror(errno));
                goto _exit;
            }
        }
        for (;;) {
            unsigned char buf[256];
            int i, length;
            unsigned short revents;

            err = poll(pfds, npfds, -1);
            if (stop || (err < 0 && errno == EINTR))
                break;
            if (err < 0) {
                error("poll failed: %s", strerror(errno));
                break;
            }

            err = snd_rawmidi_poll_descriptors_revents(input, &pfds[1], npfds - 1, &revents);
            if (err < 0) {
                error("cannot get poll events: %s", snd_strerror(errno));
                break;
            }
            if (revents & (POLLERR | POLLHUP))
                break;
            if (!(revents & POLLIN)) {
                if (pfds[0].revents & POLLIN)
                    break;
                continue;
            }

            err = snd_rawmidi_read(input, buf, sizeof(buf));
            if (err == -EAGAIN)
                continue;
            if (err < 0) {
                error("cannot read from port \"%s\": %s", port_name, snd_strerror(err));
                break;
            }
            length = 0;
            for (i = 0; i < err; ++i)
                if ((buf[i] != MIDI_CMD_COMMON_CLOCK &&
                     buf[i] != MIDI_CMD_COMMON_SENSING) ||
                    (buf[i] == MIDI_CMD_COMMON_CLOCK   && !ignore_clock) ||
                    (buf[i] == MIDI_CMD_COMMON_SENSING && !ignore_active_sensing))
                    buf[length++] = buf[i];
            if (length == 0)
                continue;
            read += length;

            // convert_data_to_keys(TODO);

            if (DEBUG)
            {
                for (i = 0; i < length; ++i)
                {
                    print_byte(buf[i]);
                }
                fflush(stdout);
            }
            parse_rx_data(kbFd, buf, length);

            if (timeout > 0) {
                err = timerfd_settime(pfds[0].fd, 0, &itimerspec, NULL);
                if (err < 0) {
                    error("cannot set timer: %s", strerror(errno));
                    break;
                }
            }
        }
        if (isatty(fileno(stdout)))
            printf("\n%d bytes read\n", read);
    }

    ok = 1;
_exit:
    if (inputp)
        snd_rawmidi_close(input);
    if (outputp)
        snd_rawmidi_close(output);
_exit2:
    if (kbFd != -1)
    {
        close_kb(kbFd);
    }

    return !ok;
}
