#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <mpv/client.h>
#include <mpv/render.h>

#include <unordered_map>
#include <deque>
#include <algorithm>

using namespace godot;


class ThumbnailPlayer : public godot::Node {
    GDCLASS(ThumbnailPlayer, godot::Node)

private:
    // --- seek control ---
    double last_seek_time_sec = -1000.0;
    double last_seek_wallclock = 0.0;

    // --- cache ---
    std::unordered_map<int, Ref<Texture2D>> cache;
    std::deque<int> lru;

    int current_bucket = -1;

    // helpers
    int quantize(double t) const;
    void touch_cache(int bucket);
    void evict_if_needed();

    static constexpr double BUCKET_SEC = 1.0;     // quantization
    static constexpr double MIN_SEEK_INTERVAL = 0.25; // hard limit
    static constexpr int    MAX_CACHE = 64;       // thumbnails

    
    // mpv
    mpv_handle *mpv = nullptr;
    mpv_render_context *mpv_ctx = nullptr;

    bool mpv_events_pending = false;
    bool frame_ready = false;

    static void mpv_wakeup(void *userdata);
    void on_mpv_wakeup();

    // thumbnail state
    int width = 256;
    int height = 144;
    int stride = 0;

    double requested_time = -1.0;
    double last_rendered_time = -1.0;

    bool frame_dirty = false;

    // buffers
    PackedByteArray pixel_data;
    Ref<ImageTexture> texture;
	Ref<Image> image;

protected:
    static void _bind_methods();

public:
    ThumbnailPlayer();
    ~ThumbnailPlayer();

    bool open(String path);

	virtual void _ready() override;
    virtual void _process(double delta) override;

    void request_thumbnail(double time_sec);
    //bool update(); // call from _process
    void poll_events();

    Ref<Texture2D> get_texture() const;
};
