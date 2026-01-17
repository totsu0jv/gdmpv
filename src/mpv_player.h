#pragma once

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include <atomic>

using namespace godot;

class MPVPlayer : public Control {
	GDCLASS(MPVPlayer, Control)

private:
	mpv_handle *mpv;
	mpv_render_context *mpv_gl;
	Ref<ImageTexture> texture;
	Ref<Image> image;

	double current_time;
	double duration;
	int video_width;
	int video_height;

	TextureRect *target_texture_rect = nullptr;
    std::atomic<bool> texture_needs_update{ false };
	bool is_buffering = false;

	bool native_subtitles_enabled = false; // Toggle for native subtitle rendering
	String last_subtitle_text = ""; // Cache last subtitle text to avoid duplicate signals

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

	virtual void _process(double delta) override;


	// Playback control
	void load_file(const String &p_path);
	void play();
	void pause();
	void stop();

    void set_target_texture_rect(TextureRect *rect);


	// Property getters
	double get_position() const { return current_time; }
	double get_duration() const;
	Vector2i get_video_size() const { return Vector2i(video_width, video_height); }

	double get_time_pos() const;
	double get_percentage_pos() const;

	void set_time_pos(double pos);

	// Settings
	void set_volume(double p_volume);
	double get_volume() const;
	void set_loop(bool p_loop);
	bool get_loop() const;

	// Advanced MPV options
	void set_mpv_property(const String &p_property, const Variant &p_value);
	Variant get_mpv_property(const String &p_property) const;
	void execute_mpv_command(const PackedStringArray &p_command);


	void set_audio_track(String id);
	void set_subtitle_track(String id);
	void add_subtitle_file(String path, String title, String lang);

	
    Array get_audio_tracks();
	Array get_subtitle_tracks();

	void set_native_subtitles_enabled(bool enabled);

	void set_subtitle_delay(String seconds);
	double get_subtitle_delay() const;

	bool is_playing() const;
	bool is_paused() const;

	
    void seek(String seconds, bool relative);
	void seek_to_percentage(String pos);
	void seek_content_pos(String pos);
};
