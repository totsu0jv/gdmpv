#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <mpv/client.h>
#include <mpv/render.h>

#include <bitset>
#include <vector>

using namespace godot;


class ThumbnailPlayer : public godot::Node {
    GDCLASS(ThumbnailPlayer, godot::Node)

private:
    // --- seek control ---
    double last_seek_time_sec = -1000.0;
    double last_seek_wallclock = 0.0;

    static constexpr int THUMB_COUNT = 100;


    std::array<Ref<ImageTexture>, THUMB_COUNT> thumbs;
    std::bitset<THUMB_COUNT> ready;

    std::vector<int> generation_order;
    int gen_cursor = 0;
    bool generating = false;


    static constexpr double MIN_SEEK_INTERVAL = 0.05; // hard limit
    //static constexpr double MIN_SEEK_INTERVAL = 0.25; // hard limit

    
    // mpv
    mpv_handle *mpv = nullptr;
    mpv_render_context *mpv_ctx = nullptr;

    bool mpv_events_pending = false;
    bool frame_ready = false;

    // thumbnail state
    int width = 128;
    int height = 72;
    int stride = 0;

    double requested_time = -1.0;
    double last_rendered_time = -1.0;

    bool frame_dirty = false;

    
    
    int find_nearest_ready(int percentage) const;
    void start_generation();
    void seek_to_bucket(int bucket);
    void render_into_texture(int bucket);
    void clear();

    // buffers
    PackedByteArray pixel_data;
    Ref<ImageTexture> texture;
	Ref<Image> image;
    Ref<ImageTexture> transparent_tex;


protected:
    static void _bind_methods();

public:
    ThumbnailPlayer();
    ~ThumbnailPlayer();

    bool open(String path);

	virtual void _ready() override;
    virtual void _process(double delta) override;

    void request_thumbnail(int percentage);
    //bool update(); // call from _process
    void poll_events();


    Ref<Texture2D> get_texture() const;
};
