#include <errno.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define DIRECTION_BAR_WIDTH 16
#define DIRECTION_BAR_HEIGHT 16
#define DIRECTON_BAR_VALUE_SCALE 1.0

float direction_value = 0.2;
float prev_direction_value = 0.2;

SDL_Window *window;
int display_width, display_height;
int window_width, window_height;

bool is_running = false;

#define CHANNELS_COUNT 2
float channels[CHANNELS_COUNT];

typedef struct {
    bool is_right;
    float channels[CHANNELS_COUNT];
} side_history;

#define MAX_HISTORY_ITEMS 32
side_history history[MAX_HISTORY_ITEMS];
int history_index = 0;

bool is_right_often()
{
    int right = 0;
    int left = 0;

    for(int i = 0; i < MAX_HISTORY_ITEMS; i++)
    {
        if(history[i].is_right) right++;
        else left++;
    }

    return right > left;
}

void add_to_history(bool is_right, float channels[CHANNELS_COUNT])
{
    history[history_index].is_right = is_right;
    for(int i = 0; i < CHANNELS_COUNT; i++)
        history[history_index].channels[i] = channels[i];
    history_index++;
    if(history_index >= MAX_HISTORY_ITEMS) history_index = 0;
}

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
    float target = lerp(prev_direction_value, direction_value, 0.35);
    float portion = target * DIRECTON_BAR_VALUE_SCALE;
    float x = portion * (float)window_width;
    float xx = ((float) window_width / 2) + x;

    prev_direction_value = direction_value;

    // XClearWindow(display, window);

    // // Draw center
    // XSetForeground(display, gc, _RGB(255, 0, 110));
    // XFillRectangle(display, window, gc, (window_width / 2) - 4, window_height - 42, 8, 42);

    // // Draw selector
    // XSetForeground(display, gc, WhitePixel(display, screenId));
    // XFillRectangle(display, window, gc, xx - (DIRECTION_BAR_WIDTH / 2), window_height - (DIRECTION_BAR_HEIGHT*2), DIRECTION_BAR_WIDTH, (DIRECTION_BAR_HEIGHT / 2));

    // // Draw second selector (with is_right_often prediction)

    // for(int i = 0; i < MAX_HISTORY_ITEMS; i++) {
    //     side_history hist = history[i];
    //     if(hist.is_right != is_right_often()) continue;

    //     float local = -hist.channels[0];
    //     if(is_right_often()) local = hist.channels[1];

    //     float target2 = local;
    //     float portion2 = target2 * DIRECTON_BAR_VALUE_SCALE;
    //     float x2 = portion2 * (float)window_width;
    //     float xx2 = ((float) window_width / 2) + x2;

    //     XSetForeground(display, gc, _RGB(0, 255, 0));
    //     XFillRectangle(display, window, gc, xx2 - (DIRECTION_BAR_WIDTH / 2), window_height - DIRECTION_BAR_HEIGHT, DIRECTION_BAR_WIDTH, (DIRECTION_BAR_HEIGHT / 2));
    // }
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

        add_to_history(channels[0] < channels[1], channels);

        data->move = true;
        fflush(stdout);

        pw_stream_queue_buffer(data->stream, b);

        // while(XPending(display))
        //     XNextEvent(display, &x11_event);
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

// void allow_x11_window_input_passthrough(Window w, Display *d)
// {
//     XserverRegion region = XFixesCreateRegion(d, NULL, 0);

//     XFixesSetWindowShapeRegion(d, w, ShapeBounding, 0, 0, 0);
//     XFixesSetWindowShapeRegion(d, w, ShapeInput, 0, 0, region);

//     XFixesDestroyRegion(d, region);
// }

bool create_window()
{
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
    {
        printf("Failed to initialize the SDL2 library: %s!\n", SDL_GetError());
        return false;
    }

    window_width = 600;
    window_height = 40;

    window = SDL_CreateWindow("sndchprev",
                window_width, window_height, 
                SDL_WINDOW_BORDERLESS 
                    | SDL_WINDOW_MAXIMIZED 
                    | SDL_WINDOW_ALWAYS_ON_TOP 
                    | SDL_WINDOW_TRANSPARENT 
                    | SDL_WINDOW_NOT_FOCUSABLE  
            );

    if(!window)
    {
        puts("Failed to create window\n");
        return false;
    }

    is_running = true;
}

int main(int argc, char *argv[]) 
{
    if(!create_window()) {
        SDL_Quit();
        return -1;
    }

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