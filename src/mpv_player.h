#pragma once

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

using namespace godot;

class MPVPlayer : public Control {
	GDCLASS(MPVPlayer, Control)

private:
	mpv_handle *mpv;
	mpv_render_context *mpv_gl;
	Ref<ImageTexture> texture;
	Ref<Image> image;

	bool is_playing;
	bool is_paused;
	double current_time;
	double duration;
	int video_width;
	int video_height;

	PackedByteArray frame_buffer;

	void initialize_mpv();
	void cleanup_mpv();
	void update_frame();
	static void on_mpv_events(void *ctx);
	static void on_mpv_render_update(void *ctx);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	MPVPlayer();
	~MPVPlayer() override;

	// Playback control
	void load_file(const String &p_path);
	void play();
	void pause();
	void stop();
	void seek(double p_position);

	// Property getters
	bool get_is_playing() const { return is_playing; }
	bool get_is_paused() const { return is_paused; }
	double get_position() const { return current_time; }
	double get_duration() const { return duration; }
	Vector2i get_video_size() const { return Vector2i(video_width, video_height); }

	// Settings
	void set_volume(double p_volume);
	double get_volume() const;
	void set_loop(bool p_loop);
	bool get_loop() const;

	// Advanced MPV options
	void set_mpv_property(const String &p_property, const Variant &p_value);
	Variant get_mpv_property(const String &p_property) const;
	void execute_mpv_command(const PackedStringArray &p_command);
};
