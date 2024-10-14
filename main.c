#include <errno.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#define DIRECTION_BAR_WIDTH 32
#define DIRECTION_BAR_HEIGHT 32
#define DIRECTON_BAR_VALUE_SCALE 1.0

float direction_value = 0.2;
float prev_direction_value = 0.2;

Display* display;
int screenId; // todo rename
Window window;
GC gc;
int display_width, display_height;
int window_width, window_height;

bool is_running = false;
XEvent x11_event;

struct data {
    struct pw_main_loop *loop;
    struct pw_stream *stream;

    struct spa_audio_info format;
    unsigned move:1;
};

float lerp(float a, float b, float f)
{
    return a * (1.0 - f) + (b * f);
}

unsigned long _RGB(int r,int g, int b)
{
    return b + (g<<8) + (r<<16);
}

void draw_direction_bar()
{
    float target = lerp(prev_direction_value, direction_value, 0.7);
    float portion = target * DIRECTON_BAR_VALUE_SCALE;
    float x = portion * (float)window_width;
    float xx = ((float) window_width / 2) + x;

    prev_direction_value = direction_value;

    XClearWindow(display, window);

    // Draw center
    XSetForeground(display, gc, _RGB(255, 0, 110));
    XFillRectangle(display, window, gc, (window_width / 2) - 4, window_height - 42, 8, 42);

    // TODO: Draw track by different color
    // Draw track
    // XSetForeground(display, gc, WhitePixel(display, screenId));
    // XFillRectangle(display, window, gc, 0, window_height - DIRECTION_BAR_HEIGHT, display_width, DIRECTION_BAR_HEIGHT);

    // Draw selector
    XSetForeground(display, gc, WhitePixel(display, screenId));
    XFillRectangle(display, window, gc, xx - (DIRECTION_BAR_WIDTH / 2), window_height - DIRECTION_BAR_HEIGHT, DIRECTION_BAR_WIDTH, (DIRECTION_BAR_HEIGHT / 2));
}

/* our data processing function is in general:
 *
 *  struct pw_buffer *b;
 *  b = pw_stream_dequeue_buffer(stream);
 *
 *  .. consume stuff in the buffer ...
 *
 *  pw_stream_queue_buffer(stream, b);
 */
static void on_process(void *userdata) 
{
        struct data *data = userdata;
        struct pw_buffer *b;
        struct spa_buffer *buf;
        float *samples, max;
        uint32_t c, n, n_channels, n_samples, peak;

        if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL) {
                pw_log_warn("out of buffers: %m");
                return;
        }

        buf = b->buffer;
        if ((samples = buf->datas[0].data) == NULL)
                return;

        n_channels = data->format.info.raw.channels;
        n_samples = buf->datas[0].chunk->size / sizeof(float);

        direction_value = 0.0;

        #define CHANNELS_COUNT 2
        float channels[CHANNELS_COUNT];

        /* move cursor up */
        if (data->move)
                fprintf(stdout, "%c[%dA", 0x1b, n_channels + 1);
        fprintf(stdout, "captured %d samples\n", n_samples / n_channels);
        for (c = 0; c < data->format.info.raw.channels; c++) {
                max = 0.0f;
                for (n = c; n < n_samples; n += n_channels)
                        max =   (max, fabsf(samples[n]));

                peak = (uint32_t)SPA_CLAMPF(max * 30, 0.f, 39.f);

                fprintf(stdout, "channel %d: |%*s%*s| peak:%f\n",
                                c, peak+1, "*", 40 - peak, "", max);

            channels[c] = max;
        }

        // Match loudest channel
        if(channels[0] < channels[1]) { // RIGHT>
            direction_value = channels[1];
        } else { // LEFT>
            direction_value = -channels[0];
        }

        data->move = true;
        fflush(stdout);

        pw_stream_queue_buffer(data->stream, b);

        while(XPending(display))
            XNextEvent(display, &x11_event);
        fprintf(stdout, "direction_value is %f\n", direction_value);
        fflush(stdout);
        draw_direction_bar();
        // TODO: Respect is_running
}

/* Be notified when the stream param changes. We're only looking at the
 * format changes.
 */
static void on_stream_param_changed(void *_data, uint32_t id, const struct spa_pod *param) 
{
        struct data *data = _data;

        /* NULL means to clear the format */
        if (param == NULL || id != SPA_PARAM_Format)
                return;

        if (spa_format_parse(param, &data->format.media_type, &data->format.media_subtype) < 0)
                return;

        /* only accept raw audio */
        if (data->format.media_type != SPA_MEDIA_TYPE_audio ||
            data->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
                return;

        /* call a helper function to parse the format for us. */
        spa_format_audio_raw_parse(param, &data->format.info.raw);

        fprintf(stdout, "capturing rate:%d channels:%d\n",
                        data->format.info.raw.rate, data->format.info.raw.channels);
}

static const struct pw_stream_events stream_events = {
        PW_VERSION_STREAM_EVENTS,
        .param_changed = on_stream_param_changed,
        .process = on_process,
};

static void do_quit(void *userdata, int signal_number) 
{
        struct data *data = userdata;
        pw_main_loop_quit(data->loop);
}

void allow_x11_window_input_passthrough(Window w, Display *d)
{
    XserverRegion region = XFixesCreateRegion(d, NULL, 0);

    XFixesSetWindowShapeRegion(d, w, ShapeBounding, 0, 0, 0);
    XFixesSetWindowShapeRegion(d, w, ShapeInput, 0, 0, region);

    XFixesDestroyRegion(d, region);
}

void create_x11_window()
{
    display = XOpenDisplay(NULL);

    XVisualInfo vinfo;
    XMatchVisualInfo(display, DefaultScreen(display), 32, TrueColor, &vinfo);

    XSetWindowAttributes attr;
    attr.colormap = XCreateColormap(display, DefaultRootWindow(display), vinfo.visual, AllocNone);
    attr.border_pixel = 0;
    attr.background_pixel = 0;

    // TODO: Replace with full screen values
    window_width = 900;
    window_height = 80;

    window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, window_width, window_height, 0, vinfo.depth, InputOutput, vinfo.visual, CWColormap | CWBorderPixel | CWBackPixel, &attr);
    XSelectInput(display, window, StructureNotifyMask);
    gc = XCreateGC(display, window, 0, 0);

    int s = DefaultScreen(display);
    screenId = s;
    display_height = DisplayHeight(display, s);
    display_width = DisplayWidth(display, s);

    Window root = RootWindow(display, s);
    allow_x11_window_input_passthrough(window, display);
    
    Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", 0);
    XSetWMProtocols(display, window, &wm_delete_window, 1);

    XMapWindow(display, window);

    is_running = true;
}

int main(int argc, char *argv[]) 
{
    create_x11_window();

        struct data data = { 0, };
        const struct spa_pod *params[1];
        uint8_t buffer[1024];
        struct pw_properties *props;
        struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

        pw_init(&argc, &argv);

        /* make a main loop. If you already have another main loop, you can add
         * the fd of this pipewire mainloop to it. */
        data.loop = pw_main_loop_new(NULL);

        pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGINT, do_quit, &data);
        pw_loop_add_signal(pw_main_loop_get_loop(data.loop), SIGTERM, do_quit, &data);

        /* Create a simple stream, the simple stream manages the core and remote
         * objects for you if you don't need to deal with them.
         *
         * If you plan to autoconnect your stream, you need to provide at least
         * media, category and role properties.
         *
         * Pass your events and a user_data pointer as the last arguments. This
         * will inform you about the stream state. The most important event
         * you need to listen to is the process event where you need to produce
         * the data.
         */
        props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                        PW_KEY_CONFIG_NAME, "client-rt.conf",
                        PW_KEY_MEDIA_CATEGORY, "Capture",
                        PW_KEY_MEDIA_ROLE, "Music",
                        NULL);
        if (argc > 1)
                /* Set stream target if given on command line */
                pw_properties_set(props, PW_KEY_TARGET_OBJECT, argv[1]);

        /* uncomment if you want to capture from the sink monitor ports */
        /* pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true"); */

        data.stream = pw_stream_new_simple(
                        pw_main_loop_get_loop(data.loop),
                        "audio-capture",
                        props,
                        &stream_events,
                        &data);

        /* Make one parameter with the supported formats. The SPA_PARAM_EnumFormat
         * id means that this is a format enumeration (of 1 value).
         * We leave the channels and rate empty to accept the native graph
         * rate and channels. */
        params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
                        &SPA_AUDIO_INFO_RAW_INIT(
                                .format = SPA_AUDIO_FORMAT_F32));

        /* Now connect this stream. We ask that our process function is
         * called in a realtime thread. */
        pw_stream_connect(data.stream,
                          PW_DIRECTION_INPUT,
                          PW_ID_ANY,
                          PW_STREAM_FLAG_AUTOCONNECT |
                          PW_STREAM_FLAG_MAP_BUFFERS |
                          PW_STREAM_FLAG_RT_PROCESS,
                          params, 1);

        /* and wait while we let things run */
        pw_main_loop_run(data.loop);

        pw_stream_destroy(data.stream);
        pw_main_loop_destroy(data.loop);
        pw_deinit();

        return 0;
}