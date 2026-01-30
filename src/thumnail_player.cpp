#include "thumnail_player.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/time.hpp>

ThumbnailPlayer::ThumbnailPlayer() {
    mpv = mpv_create();
    ERR_FAIL_COND(mpv == nullptr);

    mpv_set_option_string(mpv, "pause", "yes");
    mpv_set_option_string(mpv, "audio", "no");
    mpv_set_property_string(mpv, "aid", "no");
    mpv_set_option_string(mpv, "sid", "no");
    mpv_set_option_string(mpv, "osc", "no");
    mpv_set_option_string(mpv, "hwdec", "no");
    mpv_set_option_string(mpv, "vo", "libmpv");
    mpv_set_property_string(mpv, "osc", "no");
    mpv_set_property_string(mpv, "sub", "no");
    mpv_set_property_string(mpv, "osd-level", "0");

    mpv_set_property_string(mpv, "hr-seek", "no");
    mpv_set_property_string(mpv, "video-sync", "desync");

    mpv_set_option_string(mpv, "vd-lavc-fast", "yes");
    mpv_set_option_string(mpv, "vd-lavc-threads", "2");

    mpv_set_property_string(mpv, "vf", "scale=128:72:flags=fast_bilinear");


    mpv_initialize(mpv);

    // --- render context (CPU) ---
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_SW},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    int err = mpv_render_context_create(&mpv_ctx, mpv, params);
    ERR_FAIL_COND(err < 0);

    //mpv_set_wakeup_callback(mpv, &ThumbnailPlayer::mpv_wakeup, this);

    stride = width * 4;
    pixel_data.resize(stride * height);


    transparent_tex = ImageTexture::create_from_image(Image::create_empty(width, height, false, Image::FORMAT_RGBA8));
    transparent_tex->get_image()->fill(Color(0,0,0,0));
    //texture.instantiate();
}

ThumbnailPlayer::~ThumbnailPlayer() {
    if (mpv_ctx) {
        mpv_render_context_free(mpv_ctx);
        mpv_ctx = nullptr;
    }
    if (mpv) {
        mpv_terminate_destroy(mpv);
        mpv = nullptr;
    }
}

bool ThumbnailPlayer::open(String path) {
    if (!mpv) return false;

    clear();

    CharString p = path.utf8();
    const char *cmd[] = {
        "loadfile",
        p.get_data(),
        "replace",
        nullptr
    };

    UtilityFunctions::print(vformat("ThumbnailPlayer: Opening file: %s", path));

    return mpv_command(mpv, cmd) >= 0;
}

void ThumbnailPlayer::clear() {
    ready.reset();
    generation_order.clear();
    gen_cursor = 0;
    generating = false;

    for (auto &t : thumbs) {
        t.unref();
    }

}

int ThumbnailPlayer::find_nearest_ready(int index) const {
    if (index < 0 || index >= THUMB_COUNT)
        return -1;

    if (ready.test(index))
        return index;

    int max_dist = 3;

    for (int d = 1; d <= max_dist; d++) {
        int l = index - d;
        int r = index + d;

        if (l >= 0 && ready.test(l))
            return l;
        if (r < THUMB_COUNT && ready.test(r))
            return r;
    }

    return -1;
}



void ThumbnailPlayer::start_generation() {
    generation_order.clear();

    for (int i = 0; i < THUMB_COUNT; i += 2)
        generation_order.push_back(i);

    for (int i = 1; i < THUMB_COUNT; i += 2)
        generation_order.push_back(i);

    gen_cursor = 0;
    generating = true;

    seek_to_bucket(generation_order[0]);
}

void ThumbnailPlayer::request_thumbnail(int percentage) {
    if (!mpv) return;

    double now = Time::get_singleton()->get_ticks_msec() / 1000.0;
    if (now - last_seek_wallclock < MIN_SEEK_INTERVAL)
        return;

    last_seek_wallclock = now;

    int nearest = find_nearest_ready(percentage);

    if (nearest == -1) {
        emit_signal("thumbnail_generated", transparent_tex);
        return;
    }

    emit_signal("thumbnail_generated", thumbs[nearest]);
}

void ThumbnailPlayer::seek_to_bucket(int bucket) {
    //double t = duration * (double(bucket) / (THUMB_COUNT - 1));

    const char *cmd[] = {
        "seek",
        String::num(bucket).utf8().get_data(),
        "absolute-percent+keyframes",
        nullptr
    };

    mpv_command_async(mpv, 0, cmd);
}



void ThumbnailPlayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open", "path"), &ThumbnailPlayer::open);
    ClassDB::bind_method(D_METHOD("request_thumbnail", "percentage"), &ThumbnailPlayer::request_thumbnail);
    //ClassDB::bind_method(D_METHOD("update"), &ThumbnailPlayer::update);
    //ClassDB::bind_method(D_METHOD("get_texture"), &ThumbnailPlayer::get_texture);

	ADD_SIGNAL(MethodInfo("thumbnail_generated", PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D")));

}


void ThumbnailPlayer::poll_events() {
   /*  if (!mpv_events_pending) return; */
   /*  mpv_events_pending = false; */

    while (true) {
        mpv_event *ev = mpv_wait_event(mpv, 0);
        if (ev->event_id == MPV_EVENT_NONE)
            break;

        switch (ev->event_id) {

        case MPV_EVENT_FILE_LOADED:
            // ready to seek
            start_generation();
            break;

        case MPV_EVENT_SEEK:
            // seek started
            frame_ready = false;
            break;

        case MPV_EVENT_PLAYBACK_RESTART:
            // seek completed, new frame available
            frame_ready = true;
            break;

        case MPV_EVENT_SHUTDOWN:
            mpv_events_pending = false;
            frame_ready = false;
            return;

        default:
            break;
        }
    }
}

void ThumbnailPlayer::_process(double delta) {
    poll_events();

    if (!frame_ready || !generating)
        return;

    frame_ready = false;

    int bucket = generation_order[gen_cursor];

    render_into_texture(bucket);
    //ready.set(bucket);

    gen_cursor++;

    if (gen_cursor < generation_order.size()) {
        seek_to_bucket(generation_order[gen_cursor]);
    } else {
        generating = false;
    }

}

void ThumbnailPlayer::render_into_texture(int index) {
    if (!mpv_ctx)
        return;

    int size[2] = { width, height };
    int stride = width * 4;
	const char *format = "rgba";

    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_SW_SIZE, size },
        { MPV_RENDER_PARAM_SW_FORMAT, const_cast<char *>(format)  },
        { MPV_RENDER_PARAM_SW_STRIDE, &stride },
        { MPV_RENDER_PARAM_SW_POINTER, pixel_data.ptrw() },
        { MPV_RENDER_PARAM_INVALID, nullptr }
    };

    mpv_render_context_render(mpv_ctx, params);

    Ref<Image> img = Image::create_from_data(
        width,
        height,
        false,
        Image::FORMAT_RGBA8,
        pixel_data
    );

    Ref<ImageTexture> tex;
    if (thumbs[index].is_valid()) {
        tex = thumbs[index];
        tex->set_image(img);
    } else {
        tex = ImageTexture::create_from_image(img);
        thumbs[index] = tex;
    }

    ready.set(index);
}


void ThumbnailPlayer::_ready() {
	image = Image::create_empty(width, height, false, Image::FORMAT_RGBA8);
	texture = ImageTexture::create_from_image(image);
}
