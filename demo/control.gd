extends Control
@onready var mpv_player: MPVPlayer = $MPVPlayer
@onready var texture_rect: TextureRect = $TextureRect


# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	mpv_player.load_file("http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4")

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	if not texture_rect.texture:
		mpv_player.set_target_texture_rect(texture_rect)
