#include "register_types.h"
#include "core/class_db.h"
#include "audio_stream_ext.hpp"

void register_gdaudioext_types() {
	ClassDB::register_class<AudioStreamExt>();
}

void unregister_gdaudioext_types() {
}
