# gdaudioext - Godot Audio [Extended](https://en.wikipedia.org/wiki/Libavcodec#Implemented_audio_codecs) Support

### Basic Usage:

This module uses a class called `AudioStreamExt` to load and play audio. Attach it to an `AudioStreamPlayer` like any other stream.

```py
# Example usage of gdaudioext in Godot Engine.
# https://github.com/nathanfranke/gdaudioext/
extends Node

# There should be an AudioStreamPlayer child of this node.
onready var player: AudioStreamPlayer = $AudioStreamPlayer
# Reference to audio stream.
var stream := AudioStreamExt.new()

func _ready() -> void:
	# Opus Example
	stream.create("https://opus-codec.org/static/examples/samples/music_orig.wav")
	
	# MP3 Example
	#stream.create("https://file-examples-com.github.io/uploads/2017/11/file_example_MP3_5MG.mp3")
	
	# Assign the stream and play it.
	player.stream = stream
	player.play()

# This part is optional, but useful to show the module's capabilities.
func _process(_delta: float) -> void:
	# Seek left and right 5 seconds.
	if Input.is_action_just_pressed("ui_left"):
		player.seek(player.get_playback_position() - 5.0)
	if Input.is_action_just_pressed("ui_right"):
		player.seek(player.get_playback_position() + 5.0)
	
	# Print current time and duration.
	print("%.3f/%.3f" % [player.get_playback_position(), stream.get_length()])

```
