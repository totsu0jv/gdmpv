#include "mpv_player.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

MPVPlayer::MPVPlayer() :
		target_texture_rect(nullptr),
		is_buffering(false),
		texture_needs_update(false) {
	mpv = nullptr;
	mpv_gl = nullptr;
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
	mpv = mpv_create();
	if (!mpv) {
		UtilityFunctions::push_error("MPV: Failed to create MPV instance");
		return;
	}

	UtilityFunctions::print("MPV: Instance created successfully");

	// Set options BEFORE initialization
	int ret;

	// Enable terminal for debugging
	ret = mpv_set_option_string(mpv, "terminal", "yes");
	if (ret < 0) {
		UtilityFunctions::push_warning("MPV: Failed to enable terminal output");
	}

	// Set video output to libmpv
	ret = mpv_set_option_string(mpv, "vo", "libmpv");
	if (ret < 0) {
		UtilityFunctions::push_error("MPV: Failed to set vo=libmpv");
	}

	// Try to enable hardware decoding
	ret = mpv_set_option_string(mpv, "hwdec", "auto-safe");
	if (ret < 0) {
		UtilityFunctions::push_warning("MPV: Failed to set hwdec");
	}

	// Audio settings
	ret = mpv_set_option_string(mpv, "audio-client-name", "Godot MPV Player");

	// Keep audio device open
	ret = mpv_set_option_string(mpv, "keep-open", "yes");

	mpv_set_option_string(mpv, "profile", "fast");
	mpv_set_option_string(mpv, "video-sync", "display");

	mpv_set_option_string(mpv, "network-timeout", "15");
	mpv_set_option_string(mpv, "user-agent", "Stremio");


	mpv_set_option_string(mpv, "network-timeout", "60");
	mpv_set_option_string(mpv, "demuxer-readahead-secs", "20");
	mpv_set_option_string(mpv, "cache", "yes");
	mpv_set_option_string(mpv, "cache-secs", "15");
	mpv_set_option_string(mpv, "force-seekable", "yes");
	mpv_set_option_string(mpv, "hr-seek", "yes");
	mpv_set_option_string(mpv, "hr-seek-demuxer-offset", "1.5");
	mpv_set_option_string(mpv, "stream-buffer-size", "10M");

	// Initialize MPV
	ret = mpv_initialize(mpv);
	if (ret < 0) {
		UtilityFunctions::push_error(vformat("MPV: Failed to initialize MPV: %s", mpv_error_string(ret)));
		mpv_terminate_destroy(mpv);
		mpv = nullptr;
		return;
	}

	UtilityFunctions::print("MPV: Initialized successfully");

	mpv_observe_property(mpv, 2, "paused-for-cache", MPV_FORMAT_FLAG);
	mpv_observe_property(mpv, 3, "core-idle", MPV_FORMAT_FLAG);
	mpv_observe_property(mpv, 4, "sub-text", MPV_FORMAT_STRING);

	// Request log messages at info level for debugging
	mpv_request_log_messages(mpv, "info");

	// Set up software rendering context
	mpv_render_param params[] = {
		{ MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW) },
		{ MPV_RENDER_PARAM_INVALID, nullptr }
	};

	ret = mpv_render_context_create(&mpv_gl, mpv, params);
	if (ret < 0) {
		UtilityFunctions::push_error(vformat("MPV: Failed to create render context: %s", mpv_error_string(ret)));
		mpv_terminate_destroy(mpv);
		mpv = nullptr;
		return;
	}

	UtilityFunctions::print("MPV: Render context created successfully");

	// Set update callback
	mpv_render_context_set_update_callback(mpv_gl, on_mpv_render_update, this);
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
        player->texture_needs_update.store(true, std::memory_order_relaxed);
    }
}

void MPVPlayer::update_frame() {
	if (!mpv_gl) {
		UtilityFunctions::push_warning("MPV: update_frame called but no render context");
		return;
	}

	// Get video dimensions
	Variant width_var = get_mpv_property("width");
	Variant height_var = get_mpv_property("height");

	if (width_var.get_type() == Variant::NIL || height_var.get_type() == Variant::NIL) {
		UtilityFunctions::push_warning("MPV: Video dimensions not available yet");
		return;
	}

	int64_t width = width_var.operator int64_t();
	int64_t height = height_var.operator int64_t();

	if (width <= 0 || height <= 0) {
		UtilityFunctions::push_warning(vformat("MPV: Invalid video dimensions: %dx%d", width, height));
		return;
	}

	// Update dimensions if changed
	if (video_width != (int)width || video_height != (int)height) {
		video_width = (int)width;
		video_height = (int)height;
		UtilityFunctions::print(vformat("MPV: Video size: %dx%d", video_width, video_height));
	}

	// Prepare frame buffer
	int frame_size = video_width * video_height * 4; // RGBA
	if (frame_buffer.size() != frame_size) {
		frame_buffer.resize(frame_size);
		UtilityFunctions::print(vformat("MPV: Frame buffer resized to %d bytes", frame_size));
	}

	// Render frame - use proper lvalue variables
	int size[2] = { video_width, video_height };
	int stride = video_width * 4;
	const char *format = "rgba";

	mpv_render_param render_params[] = {
		{ MPV_RENDER_PARAM_SW_SIZE, size },
		{ MPV_RENDER_PARAM_SW_FORMAT, const_cast<char *>(format) },
		{ MPV_RENDER_PARAM_SW_STRIDE, &stride },
		{ MPV_RENDER_PARAM_SW_POINTER, frame_buffer.ptrw() },
		{ MPV_RENDER_PARAM_INVALID, nullptr }
	};

	int ret = mpv_render_context_render(mpv_gl, render_params);
	if (ret < 0) {
		UtilityFunctions::push_error(vformat("MPV: Render failed: %s", mpv_error_string(ret)));
		return;
	}

	// Update texture
	if (image.is_null()) {
		image.instantiate();
		UtilityFunctions::print("MPV: Image instance created");
	}

	image->set_data(video_width, video_height, false, Image::FORMAT_RGBA8, frame_buffer);

	if (texture.is_null()) {
		texture.instantiate();
		UtilityFunctions::print("MPV: Texture instance created");
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

				// Log all events for debugging
				//UtilityFunctions::print(vformat("MPV Event: %d", event->event_id));

				switch (event->event_id) {
					case MPV_EVENT_PLAYBACK_RESTART:
						UtilityFunctions::print("MPV: Playback started/restarted");
						break;
					case MPV_EVENT_END_FILE: {
						mpv_event_end_file *ef = (mpv_event_end_file *)event->data;
						UtilityFunctions::print(vformat("MPV: End file, reason: %d", ef->reason));
						if (ef->reason == MPV_END_FILE_REASON_EOF) {
							call_deferred("playback_finished");
						} else if (ef->reason == MPV_END_FILE_REASON_ERROR) {
							UtilityFunctions::push_error(vformat("MPV: Playback error: %s", mpv_error_string(ef->error)));
						}
						break;
					}
					case MPV_EVENT_FILE_LOADED:
						duration = get_mpv_property("duration").operator double();
						UtilityFunctions::print(vformat("MPV: File loaded, duration: %f", duration));
						emit_signal("file_loaded");
						break;
					case MPV_EVENT_LOG_MESSAGE: {
						mpv_event_log_message *msg = (mpv_event_log_message *)event->data;
						UtilityFunctions::print(vformat("MPV [%s]: %s", msg->prefix, msg->text));
						break;
					}
					case MPV_EVENT_START_FILE:
						UtilityFunctions::print("MPV: Starting file");
						break;
					case MPV_EVENT_VIDEO_RECONFIG:
						UtilityFunctions::print("MPV: Video reconfigured");
						break;
					case MPV_EVENT_AUDIO_RECONFIG:
						UtilityFunctions::print("MPV: Audio reconfigured");
						break;
					case MPV_EVENT_PROPERTY_CHANGE: {
						mpv_event_property *prop = static_cast<mpv_event_property *>(event->data);
						if (!prop || !prop->data)
							break;
						switch (event->reply_userdata) {
							/* case 1:
								time_pos = *static_cast<double *>(prop->data);
								call_deferred("emit_signal", "time_changed", time_pos);
								break;*/
							case 2: {
								bool paused_for_cache = *static_cast<int *>(prop->data) != 0;
								if (paused_for_cache && !is_buffering) {
									is_buffering = true;
									call_deferred("emit_signal", "buffering_started");
								} else if (!paused_for_cache && is_buffering) {
									is_buffering = false;
									call_deferred("emit_signal", "buffering_ended");
								}
								break;
							}
							case 3: {
								bool core_idle = *static_cast<int *>(prop->data);
								if (core_idle && !is_buffering) {
									is_buffering = true;
									call_deferred("emit_signal", "buffering_started");
								} else if (!core_idle && is_buffering) {
									is_buffering = false;
									call_deferred("emit_signal", "buffering_ended");
								}
								break;
							}
							case 4: {
								if (prop->format == MPV_FORMAT_STRING && prop->data) {
									char *sub_text = *static_cast<char **>(prop->data);
									if (sub_text != nullptr) {
										String subtitle_text = String::utf8(sub_text);
										// Only emit if text changed to avoid spam
										if (subtitle_text != last_subtitle_text) {
											last_subtitle_text = subtitle_text;
											call_deferred("emit_signal", "subtitle_changed", subtitle_text);
										}
									}
								} else {
									// No subtitle or subtitle cleared
									if (!last_subtitle_text.is_empty()) {
										last_subtitle_text = "";
										call_deferred("emit_signal", "subtitle_changed", String(""));
									}
								}
							}
						}
						break;
					}
					default:
						break;
				}
			}
			break;
		} 
	}
}

void MPVPlayer::_process(double delta) {
	// Check if we need to update the texture
	if (texture_needs_update.load()) {
		// Reset the flag at the beginning to avoid missing frames
		texture_needs_update.store(false);

		update_frame();

		//_update_texture_internal();
	}
}

void MPVPlayer::set_target_texture_rect(TextureRect *rect) {
	target_texture_rect = rect;

	// If we already have a texture, apply it immediately
	if (target_texture_rect && texture.is_valid()) {
		target_texture_rect->set_texture(texture);
	}
}

void MPVPlayer::load_file(const String &p_path) {
	if (!mpv) {
		UtilityFunctions::push_error("MPV: Cannot load file, mpv not initialized");
		return;
	}

	UtilityFunctions::print(vformat("MPV: Loading file: %s", p_path));

	const char *cmd[] = { "loadfile", p_path.utf8().get_data(), nullptr };
	int ret = mpv_command(mpv, cmd);
	if (ret < 0) {
		UtilityFunctions::push_error(vformat("MPV: Failed to load file: %s", mpv_error_string(ret)));
		return;
	}

	// Observe time position and pause state
	mpv_observe_property(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
	mpv_observe_property(mpv, 1, "pause", MPV_FORMAT_FLAG);

	UtilityFunctions::print("MPV: Load command sent successfully");
}

void MPVPlayer::play() {
	if (!mpv)
		return;
	const char *cmd[] = { "set", "pause", "no", nullptr };
	mpv_command_async(mpv, 0, cmd);
}

void MPVPlayer::pause() {
	if (!mpv)
		return;
	const char *cmd[] = { "set", "pause", "yes", nullptr };
	mpv_command_async(mpv, 0, cmd);
}

void MPVPlayer::stop() {
	if (!mpv)
		return;
	const char *cmd[] = { "stop", nullptr };
	mpv_command(mpv, cmd);
	current_time = 0.0;
}

void MPVPlayer::seek(String seconds, bool relative) {
	if (!mpv)
		return;
	const char *seek_cmd[] = { "seek", seconds.utf8().get_data(), relative ? "relative" : "absolute", nullptr };
	mpv_command(mpv, seek_cmd);
}

void MPVPlayer::seek_to_percentage(String pos) {
	if (!mpv) {
		ERR_PRINT("MPV not initialized");
		return;
	}
	const char *seek_cmd[] = { "seek", pos.utf8().get_data(), "absolute-percent", nullptr };
	mpv_command(mpv, seek_cmd);
}

void MPVPlayer::seek_content_pos(String pos) {
	if (!mpv) {
		ERR_PRINT("MPV not initialized");
		return;
	}
	const char *seek_cmd[] = { "seek", pos.utf8().get_data(), "absolute", nullptr };
	mpv_command(mpv, seek_cmd);
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

void MPVPlayer::set_audio_track(String id) {
	if (!mpv) {
		ERR_PRINT("MPV not initialized");
		return;
	}
	const char *cmd[] = { "set", "aid", id.utf8().get_data(), nullptr };
	mpv_command_async(mpv, 0, cmd);
}

void MPVPlayer::set_subtitle_track(String id) {
	if (!mpv) {
		ERR_PRINT("MPV not initialized");
		return;
	}
	const char *cmd[] = { "set", "sid", id.utf8().get_data(), nullptr };
	mpv_command_async(mpv, 0, cmd);
}

double MPVPlayer::get_duration() const {
	if (!mpv)
		return 0.0;
	
	double value = 0.0;

	if (mpv_get_property(mpv, "duration", MPV_FORMAT_DOUBLE, &value) < 0) {
		return 0.0;
	}

	return value;
}

double MPVPlayer::get_percentage_pos() const {
	if (!mpv)
		return 0.0;

	double value = 0.0;

	if (mpv_get_property(mpv, "percent-pos", MPV_FORMAT_DOUBLE, &value) < 0) {
		return 0.0;
	}

	return value;
}


Array MPVPlayer::get_audio_tracks() {
	Array tracks;

	if (!mpv) {
		return tracks;
	}

	mpv_node track_list;
	if (mpv_get_property(mpv, "track-list", MPV_FORMAT_NODE, &track_list) < 0) {
		return tracks;
	}

	if (track_list.format != MPV_FORMAT_NODE_ARRAY) {
		mpv_free_node_contents(&track_list);
		return tracks;
	}

	for (int i = 0; i < track_list.u.list->num; i++) {
		mpv_node *track = &track_list.u.list->values[i];

		if (track->format != MPV_FORMAT_NODE_MAP) {
			continue;
		}

		Dictionary track_info;
		const char *type = nullptr;

		for (int j = 0; j < track->u.list->num; j++) {
			const char *key = track->u.list->keys[j];
			mpv_node *value = &track->u.list->values[j];

			if (strcmp(key, "type") == 0 && value->format == MPV_FORMAT_STRING) {
				type = value->u.string;
			} else if (strcmp(key, "id") == 0 && value->format == MPV_FORMAT_INT64) {
				track_info["id"] = (int)value->u.int64;
			} else if (strcmp(key, "lang") == 0 && value->format == MPV_FORMAT_STRING) {
				track_info["lang"] = String::utf8(value->u.string);
			} else if (strcmp(key, "title") == 0 && value->format == MPV_FORMAT_STRING) {
				track_info["title"] = String::utf8(value->u.string);
			} else if (strcmp(key, "selected") == 0 && value->format == MPV_FORMAT_FLAG) {
				track_info["selected"] = (bool)value->u.flag;
			}
		}

		if (type && strcmp(type, "audio") == 0) {
			tracks.append(track_info);
		}
	}

	mpv_free_node_contents(&track_list);
	return tracks;
}

Array MPVPlayer::get_subtitle_tracks() {
	Array tracks;

	if (!mpv) {
		return tracks;
	}

	mpv_node track_list;
	if (mpv_get_property(mpv, "track-list", MPV_FORMAT_NODE, &track_list) < 0) {
		return tracks;
	}

	if (track_list.format != MPV_FORMAT_NODE_ARRAY) {
		mpv_free_node_contents(&track_list);
		return tracks;
	}

	for (int i = 0; i < track_list.u.list->num; i++) {
		mpv_node *track = &track_list.u.list->values[i];

		if (track->format != MPV_FORMAT_NODE_MAP) {
			continue;
		}

		Dictionary track_info;
		const char *type = nullptr;

		for (int j = 0; j < track->u.list->num; j++) {
			const char *key = track->u.list->keys[j];
			mpv_node *value = &track->u.list->values[j];

			if (strcmp(key, "type") == 0 && value->format == MPV_FORMAT_STRING) {
				type = value->u.string;
			} else if (strcmp(key, "id") == 0 && value->format == MPV_FORMAT_INT64) {
				track_info["id"] = (int)value->u.int64;
			} else if (strcmp(key, "lang") == 0 && value->format == MPV_FORMAT_STRING) {
				track_info["lang"] = String::utf8(value->u.string);
			} else if (strcmp(key, "title") == 0 && value->format == MPV_FORMAT_STRING) {
				track_info["title"] = String::utf8(value->u.string);
			} else if (strcmp(key, "selected") == 0 && value->format == MPV_FORMAT_FLAG) {
				track_info["selected"] = (bool)value->u.flag;
			}
		}

		if (type && strcmp(type, "sub") == 0) {
			tracks.append(track_info);
		}
	}

	mpv_free_node_contents(&track_list);
	return tracks;
}

void MPVPlayer::add_subtitle_file(String path, String title, String lang) {
	if (!mpv) {
		ERR_PRINT("MPV not initialized");
		return;
	}

	CharString cs = path.utf8();
	const char *c_path = cs.get_data();

	if (c_path == nullptr || c_path[0] == '\0') {
		UtilityFunctions::print("ERROR: Invalid empty subtitle path");
		return;
	}

	CharString title_cs = title.utf8();
	CharString lang_cs = lang.utf8();
	const char *cmd[] = { "sub-add", c_path, "auto", title_cs.get_data(), lang_cs.get_data(), nullptr };
	mpv_command_async(mpv, 0, cmd);
}

void MPVPlayer::set_native_subtitles_enabled(bool enabled) {
	if (!mpv) {
		return;
	}

	native_subtitles_enabled = enabled;

	if (enabled) {
		mpv_set_option_string(mpv, "sub-visibility", "yes");
	} else {
		mpv_set_option_string(mpv, "sub-visibility", "no");
	}
}

void MPVPlayer::set_subtitle_delay(String seconds) {
	if (!mpv) {
		return;
	}

	//mpv_set_property_string(mpv, "sub-delay", seconds);

	const char *cmd[] = { "set", "sub-delay", seconds.utf8().get_data(), nullptr };
	mpv_command_async(mpv, 0, cmd);
}

double MPVPlayer::get_subtitle_delay() const {
	if (!mpv)
		return 0.0;

	double value = 0.0;

	if (mpv_get_property(mpv, "sub-delay", MPV_FORMAT_DOUBLE, &value) < 0) {
		return 0.0;
	}

	return value;
}

double MPVPlayer::get_time_pos() const {
	if (!mpv)
		return 0.0;

	double value = 0.0;

	if (mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &value) < 0) {
		return 0.0;
	}

	return value;
}

bool MPVPlayer::is_playing() const {
	return !is_paused();
}

bool MPVPlayer::is_paused() const {
	if (!mpv)
		return true;

	int value = true ? 1 : 0;
	if (mpv_get_property(mpv, "pause" , MPV_FORMAT_FLAG, &value) < 0) {
		return true;
	}
	return value != 0;
}

void MPVPlayer::set_time_pos(double pos) {
	if (!mpv) {
		ERR_PRINT("MPV not initialized");
		return;
	}
	mpv_set_property_string(mpv, "pause", "yes");
	mpv_set_property_async(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE, &pos);
	mpv_set_property_string(mpv, "pause", "no");
}


void MPVPlayer::_bind_methods() {
	// Playback methods
	ClassDB::bind_method(D_METHOD("load_file", "path"), &MPVPlayer::load_file);
	ClassDB::bind_method(D_METHOD("play"), &MPVPlayer::play);
	ClassDB::bind_method(D_METHOD("pause"), &MPVPlayer::pause);
	ClassDB::bind_method(D_METHOD("stop"), &MPVPlayer::stop);
	ClassDB::bind_method(D_METHOD("seek", "seconds", "relative"), &MPVPlayer::seek);
	ClassDB::bind_method(D_METHOD("seek_to_percentage", "pos"), &MPVPlayer::seek_to_percentage);
	ClassDB::bind_method(D_METHOD("seek_content_pos", "pos"), &MPVPlayer::seek_content_pos);

	// Property methods
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

	ClassDB::bind_method(D_METHOD("is_playing"), &MPVPlayer::is_playing);
	ClassDB::bind_method(D_METHOD("is_paused"), &MPVPlayer::is_paused);
	ClassDB::bind_method(D_METHOD("get_time_pos"), &MPVPlayer::get_time_pos);
	ClassDB::bind_method(D_METHOD("get_percentage_pos"), &MPVPlayer::get_percentage_pos);


	ClassDB::bind_method(D_METHOD("set_subtitle_delay", "seconds"), &MPVPlayer::set_subtitle_delay);
	ClassDB::bind_method(D_METHOD("get_subtitle_delay"), &MPVPlayer::get_subtitle_delay);

	ClassDB::bind_method(D_METHOD("set_time_pos", "pos"), &MPVPlayer::set_time_pos);
	ClassDB::bind_method(D_METHOD("set_target_texture_rect", "rect"), &MPVPlayer::set_target_texture_rect);
	ClassDB::bind_method(D_METHOD("get_audio_tracks"), &MPVPlayer::get_audio_tracks);
	ClassDB::bind_method(D_METHOD("get_subtitle_tracks"), &MPVPlayer::get_subtitle_tracks);
	//ClassDB::bind_method(D_METHOD("set_playback_speed", "speed"), &MPVPlayer::set_playback_speed);
	ClassDB::bind_method(D_METHOD("set_native_subtitles_enabled", "enabled"), &MPVPlayer::set_native_subtitles_enabled);
	ClassDB::bind_method(D_METHOD("add_subtitle_file", "path", "title", "lang"), &MPVPlayer::add_subtitle_file, DEFVAL(""), DEFVAL(""));
	ClassDB::bind_method(D_METHOD("restart"), &MPVPlayer::pause);

	ClassDB::bind_method(D_METHOD("set_audio_track", "id"), &MPVPlayer::set_audio_track);
	ClassDB::bind_method(D_METHOD("set_subtitle_track", "id"), &MPVPlayer::set_subtitle_track);

	// Properties
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume", PROPERTY_HINT_RANGE, "0,100"), "set_volume", "get_volume");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop"), "set_loop", "get_loop");

	// Signals
	ADD_SIGNAL(MethodInfo("playback_finished"));
	ADD_SIGNAL(MethodInfo("file_loaded"));

	ADD_SIGNAL(MethodInfo("buffering_started"));
	ADD_SIGNAL(MethodInfo("buffering_ended"));

	ADD_SIGNAL(MethodInfo("subtitle_changed", PropertyInfo(Variant::STRING, "text")));
}
