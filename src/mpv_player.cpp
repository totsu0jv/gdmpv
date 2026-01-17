#include "mpv_player.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

MPVPlayer::MPVPlayer() {
	mpv = nullptr;
	mpv_gl = nullptr;
	is_playing = false;
	is_paused = false;
	current_time = 0.0;
	duration = 0.0;
	video_width = 0;
	video_height = 0;

	set_process(true);
	initialize_mpv();
}

MPVPlayer::~MPVPlayer() {
	cleanup_mpv();
}

void MPVPlayer::initialize_mpv() {
	// Create MPV instance
	UtilityFunctions::print("Initializing MPV...");
	mpv = mpv_create();
	if (!mpv) {
		UtilityFunctions::push_error("Failed to create MPV instance");
		return;
	}

	// Set options
	mpv_set_option_string(mpv, "vo", "libmpv");
	mpv_set_option_string(mpv, "hwdec", "auto");
	mpv_set_option_string(mpv, "audio-client-name", "Godot MPV Player");

	// Initialize MPV
	if (mpv_initialize(mpv) < 0) {
		UtilityFunctions::push_error("Failed to initialize MPV");
		mpv_terminate_destroy(mpv);
		mpv = nullptr;
		return;
	}

	// Request log messages
	mpv_request_log_messages(mpv, "warn");

	// Set up software rendering context
	mpv_render_param params[] = {
		{ MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW) },
		{ MPV_RENDER_PARAM_INVALID, nullptr }
	};

	if (mpv_render_context_create(&mpv_gl, mpv, params) < 0) {
		UtilityFunctions::push_error("Failed to create MPV render context");
		mpv_terminate_destroy(mpv);
		mpv = nullptr;
		return;
	}

	// Set update callback
	mpv_render_context_set_update_callback(mpv_gl, on_mpv_render_update, this);

	UtilityFunctions::print("MPV initialized successfully");
}

void MPVPlayer::cleanup_mpv() {
	if (mpv_gl) {
		mpv_render_context_free(mpv_gl);
		mpv_gl = nullptr;
	}

	if (mpv) {
		mpv_terminate_destroy(mpv);
		mpv = nullptr;
	}
}

void MPVPlayer::on_mpv_render_update(void *ctx) {
	MPVPlayer *player = static_cast<MPVPlayer *>(ctx);
	if (player) {
		player->update_frame();
	}
}

void MPVPlayer::update_frame() {
	if (!mpv_gl)
		return;

	// Get video dimensions
	int64_t width = get_mpv_property("width").operator int64_t();
	int64_t height = get_mpv_property("height").operator int64_t();

	if (width <= 0 || height <= 0)
		return;

	video_width = (int)width;
	video_height = (int)height;



	// Prepare frame buffer
	int frame_size = video_width * video_height * 4; // RGBA
	if (frame_buffer.size() != frame_size) {
		frame_buffer.resize(frame_size);
	}
	int size[2] = { video_width, video_height };
	int stride = video_width * 4;

	// Render frame
	mpv_render_param render_params[] = {
		{ MPV_RENDER_PARAM_SW_SIZE, size },
		{ MPV_RENDER_PARAM_SW_FORMAT, const_cast<char *>("rgba") },
		{ MPV_RENDER_PARAM_SW_STRIDE, &stride },
		{ MPV_RENDER_PARAM_SW_POINTER, frame_buffer.ptrw() },
		{ MPV_RENDER_PARAM_INVALID, nullptr }
	};

	mpv_render_context_render(mpv_gl, render_params);

	// Update texture
	if (image.is_null()) {
		image.instantiate();
	}

	image->set_data(video_width, video_height, false, Image::FORMAT_RGBA8, frame_buffer);

	if (texture.is_null()) {
		texture.instantiate();
	}

	texture->set_image(image);
	queue_redraw();
}

void MPVPlayer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS: {
			if (!mpv)
				return;

			// Process MPV events
			while (mpv) {
				mpv_event *event = mpv_wait_event(mpv, 0);
				if (event->event_id == MPV_EVENT_NONE)
					break;

				switch (event->event_id) {
					case MPV_EVENT_PLAYBACK_RESTART:
						is_playing = true;
						is_paused = false;
						break;
					case MPV_EVENT_END_FILE:
						is_playing = false;
						emit_signal("playback_finished");
						break;
					case MPV_EVENT_FILE_LOADED:
						duration = get_mpv_property("duration").operator double();
						emit_signal("file_loaded");
						break;
					case MPV_EVENT_PROPERTY_CHANGE: {
						mpv_event_property *prop = (mpv_event_property *)event->data;
						if (prop->format == MPV_FORMAT_DOUBLE && strcmp(prop->name, "time-pos") == 0) {
							current_time = *(double *)prop->data;
						}
						break;
					}
					default:
						break;
				}
			}

			// Update render if needed
			uint64_t flags = mpv_render_context_update(mpv_gl);
			if (flags & MPV_RENDER_UPDATE_FRAME) {
				update_frame();
			}
			break;
		}
		case NOTIFICATION_DRAW: {
			if (texture.is_valid()) {
				draw_texture_rect(texture, Rect2(Vector2(), get_size()), false);
			}
			break;
		}
	}
}

void MPVPlayer::load_file(const String &p_path) {
	if (!mpv)
		return;

	const char *cmd[] = { "loadfile", p_path.utf8().get_data(), nullptr };
	mpv_command_async(mpv, 0, cmd);

	// Observe time position
	mpv_observe_property(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
}

void MPVPlayer::play() {
	if (!mpv)
		return;
	mpv_set_property_string(mpv, "pause", "no");
	is_playing = true;
	is_paused = false;
}

void MPVPlayer::pause() {
	if (!mpv)
		return;
	mpv_set_property_string(mpv, "pause", "yes");
	is_paused = true;
}

void MPVPlayer::stop() {
	if (!mpv)
		return;
	const char *cmd[] = { "stop", nullptr };
	mpv_command(mpv, cmd);
	is_playing = false;
	is_paused = false;
	current_time = 0.0;
}

void MPVPlayer::seek(double p_position) {
	if (!mpv)
		return;
	mpv_set_property_async(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE, &p_position);
}

void MPVPlayer::set_volume(double p_volume) {
	if (!mpv)
		return;
	mpv_set_property_async(mpv, 0, "volume", MPV_FORMAT_DOUBLE, &p_volume);
}

double MPVPlayer::get_volume() const {
	return get_mpv_property("volume").operator double();
}

void MPVPlayer::set_loop(bool p_loop) {
	if (!mpv)
		return;
	const char *value = p_loop ? "inf" : "no";
	mpv_set_property_string(mpv, "loop", value);
}

bool MPVPlayer::get_loop() const {
	String loop_value = get_mpv_property("loop").operator String();
	return loop_value == "inf";
}

void MPVPlayer::set_mpv_property(const String &p_property, const Variant &p_value) {
	if (!mpv)
		return;

	switch (p_value.get_type()) {
		case Variant::BOOL:
		case Variant::INT: {
			int64_t val = p_value.operator int64_t();
			mpv_set_property(mpv, p_property.utf8().get_data(), MPV_FORMAT_INT64, &val);
			break;
		}
		case Variant::FLOAT: {
			double val = p_value.operator double();
			mpv_set_property(mpv, p_property.utf8().get_data(), MPV_FORMAT_DOUBLE, &val);
			break;
		}
		case Variant::STRING: {
			String val = p_value.operator String();
			mpv_set_property_string(mpv, p_property.utf8().get_data(), val.utf8().get_data());
			break;
		}
		default:
			UtilityFunctions::push_warning("Unsupported property type");
			break;
	}
}

Variant MPVPlayer::get_mpv_property(const String &p_property) const {
	if (!mpv)
		return Variant();

	// Try as double first
	double d_val;
	if (mpv_get_property(mpv, p_property.utf8().get_data(), MPV_FORMAT_DOUBLE, &d_val) == 0) {
		return d_val;
	}

	// Try as int
	int64_t i_val;
	if (mpv_get_property(mpv, p_property.utf8().get_data(), MPV_FORMAT_INT64, &i_val) == 0) {
		return i_val;
	}

	// Try as string
	char *s_val = nullptr;
	if (mpv_get_property(mpv, p_property.utf8().get_data(), MPV_FORMAT_STRING, &s_val) == 0) {
		String result(s_val);
		mpv_free(s_val);
		return result;
	}

	return Variant();
}

void MPVPlayer::execute_mpv_command(const PackedStringArray &p_command) {
	if (!mpv || p_command.size() == 0)
		return;

	const char **cmd = new const char *[p_command.size() + 1];
	for (int i = 0; i < p_command.size(); i++) {
		cmd[i] = p_command[i].utf8().get_data();
	}
	cmd[p_command.size()] = nullptr;

	mpv_command(mpv, cmd);
	delete[] cmd;
}

void MPVPlayer::_bind_methods() {
	// Playback methods
	ClassDB::bind_method(D_METHOD("load_file", "path"), &MPVPlayer::load_file);
	ClassDB::bind_method(D_METHOD("play"), &MPVPlayer::play);
	ClassDB::bind_method(D_METHOD("pause"), &MPVPlayer::pause);
	ClassDB::bind_method(D_METHOD("stop"), &MPVPlayer::stop);
	ClassDB::bind_method(D_METHOD("seek", "position"), &MPVPlayer::seek);

	// Property methods
	ClassDB::bind_method(D_METHOD("get_is_playing"), &MPVPlayer::get_is_playing);
	ClassDB::bind_method(D_METHOD("get_is_paused"), &MPVPlayer::get_is_paused);
	ClassDB::bind_method(D_METHOD("get_position"), &MPVPlayer::get_position);
	ClassDB::bind_method(D_METHOD("get_duration"), &MPVPlayer::get_duration);
	ClassDB::bind_method(D_METHOD("get_video_size"), &MPVPlayer::get_video_size);

	// Volume and loop
	ClassDB::bind_method(D_METHOD("set_volume", "volume"), &MPVPlayer::set_volume);
	ClassDB::bind_method(D_METHOD("get_volume"), &MPVPlayer::get_volume);
	ClassDB::bind_method(D_METHOD("set_loop", "loop"), &MPVPlayer::set_loop);
	ClassDB::bind_method(D_METHOD("get_loop"), &MPVPlayer::get_loop);

	// Advanced MPV
	ClassDB::bind_method(D_METHOD("set_mpv_property", "property", "value"), &MPVPlayer::set_mpv_property);
	ClassDB::bind_method(D_METHOD("get_mpv_property", "property"), &MPVPlayer::get_mpv_property);
	ClassDB::bind_method(D_METHOD("execute_mpv_command", "command"), &MPVPlayer::execute_mpv_command);

	// Properties
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume", PROPERTY_HINT_RANGE, "0,100"), "set_volume", "get_volume");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop"), "set_loop", "get_loop");

	// Signals
	ADD_SIGNAL(MethodInfo("playback_finished"));
	ADD_SIGNAL(MethodInfo("file_loaded"));
}
