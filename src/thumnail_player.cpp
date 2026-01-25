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

    mpv_set_wakeup_callback(mpv, &ThumbnailPlayer::mpv_wakeup, this);

    stride = width * 4;
    pixel_data.resize(stride * height);

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

int ThumbnailPlayer::quantize(double t) const {
    return int(Math::floor(t / BUCKET_SEC));
}

void ThumbnailPlayer::touch_cache(int bucket) {
    lru.erase(std::remove(lru.begin(), lru.end(), bucket), lru.end());
    lru.push_front(bucket);
}

void ThumbnailPlayer::evict_if_needed() {
    while ((int)lru.size() > MAX_CACHE) {
        int old = lru.back();
        lru.pop_back();
        cache.erase(old);
    }
}

void ThumbnailPlayer::request_thumbnail(double time_sec) {
    if (!mpv) return;

    int bucket = quantize(time_sec);

    // cache hit
    auto it = cache.find(bucket);
    if (it != cache.end()) {
        texture = it->second;
        touch_cache(bucket);
        return;
    }

    double now = Time::get_singleton()->get_ticks_msec() / 1000.0;
    if (now - last_seek_wallclock < MIN_SEEK_INTERVAL)
        return;

    last_seek_wallclock = now;
    current_bucket = bucket;
    frame_ready = false;

    double seek_time = bucket * BUCKET_SEC;

    const char *cmd[] = {
        "seek",
        String::num(seek_time).utf8().get_data(),
        "absolute-percent+keyframes",
        nullptr
    };

    UtilityFunctions::print(vformat("ThumbnailPlayer: Seeking to %f percent (bucket %d)", seek_time, bucket));

    mpv_command_async(mpv, 0, cmd);
}

void ThumbnailPlayer::mpv_wakeup(void *userdata) {
    ThumbnailPlayer *self =
        static_cast<ThumbnailPlayer *>(userdata);

    UtilityFunctions::print("ThumbnailPlayer: mpv_wakeup called");

    if (self) {
        self->on_mpv_wakeup();
    }
}

void ThumbnailPlayer::on_mpv_wakeup() {
    UtilityFunctions::print("ThumbnailPlayer: on_mpv_wakeup called");
    mpv_events_pending = true;
}


void ThumbnailPlayer::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open", "path"), &ThumbnailPlayer::open);
    ClassDB::bind_method(D_METHOD("request_thumbnail", "time_sec"), &ThumbnailPlayer::request_thumbnail);
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

    if (!frame_ready || current_bucket < 0)
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

	image->set_data(width, height, false, Image::FORMAT_RGBA8, pixel_data);


    cache[current_bucket] = texture;
    touch_cache(current_bucket);
    evict_if_needed();

    texture->set_image(image);
    current_bucket = -1;
    frame_ready = false;

    emit_signal("thumbnail_generated", texture);
}

void ThumbnailPlayer::_ready() {
	image = Image::create_empty(width, height, false, Image::FORMAT_RGBA8);
	texture = ImageTexture::create_from_image(image);
}
