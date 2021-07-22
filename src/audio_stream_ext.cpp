#include "audio_stream_ext.hpp"

#include "AudioServer.hpp"
#include "OS.hpp"

using namespace godot;

void AudioStreamPlaybackExt::_register_methods() {
    register_method("is_buffering", &AudioStreamPlaybackExt::is_buffering);
}

void AudioStreamPlaybackExt::_run_seek_job(Variant p_userdata) {
	if(!buffering) {
		buffering_time = OS::get_singleton()->get_ticks_msec();
		buffering = true;
	}
	
	base->load_thread.wait_to_finish();
	
	if(base->loaded) {
		int64_t frame = int64_t(seek_pos / av_q2d(base->stream->time_base));
		
		// This call can block for several hundred milliseconds if audio from the internet is playing.
		int error = avformat_seek_file(base->format_context, base->stream->index, INT64_MIN, frame, INT64_MAX, AVSEEK_FLAG_ANY);
		ERR_FAIL_COND(error < 0);
		
		busy_seeking = false;
		
		buffering = false;
	}
}

void AudioStreamPlaybackExt::_clear_frame_buffer() {
	if(frame_read_buffer) {
		delete frame_read_buffer;
		frame_read_buffer = NULL;
		frame_read_pos = 0;
		frame_read_len = 0;
	}
}

void AudioStreamPlaybackExt::_mix_internal(AudioFrame *p_buffer, int p_frames) {
	ERR_FAIL_COND(!active);
	int error;
	
	int pos = 0;
	
	if(busy_seeking) {
		while(pos < p_frames) {
			p_buffer[pos++] = AudioFrame(0, 0);
		}
		
		return;
	}
	
	seek_thread.wait_to_finish();
	
	if(seek_job) {
		seek_job = false;
		busy_seeking = true;
		
		// Clear old frame buffer so only fresh audio plays.
		_clear_frame_buffer();
		
		// Play silence while loading.
		while(pos < p_frames) {
			p_buffer[pos++] = AudioFrame(0, 0);
		}
		
		seek_thread.start(this, "_run_seek_job");
		return;
	}
	
	while(pos < p_frames) {
		if(frame_read_pos >= frame_read_len) {
			_clear_frame_buffer();
			
			error = av_read_frame(base->format_context, base->packet);
			if(error < 0) {
				active = false;
				while(pos < p_frames) {
					p_buffer[pos++] = AudioFrame(0, 0);
				}
				break;
			}
			
			error = avcodec_send_packet(base->codec_context, base->packet);
			ERR_FAIL_COND(error < 0);
			
			error = avcodec_receive_frame(base->codec_context, base->frame);
			ERR_FAIL_COND(error < 0);
			
			int alloc_size = av_samples_get_buffer_size(NULL, base->stream->codecpar->channels, base->frame->nb_samples, AudioStreamExt::DESTINATION_FORMAT, 0);
			frame_read_buffer = (float *)malloc(alloc_size);
			frame_read_pos = 0;
			frame_read_len = base->frame->nb_samples * base->stream->codecpar->channels;
			
			swr_convert(base->swr, (uint8_t **) &frame_read_buffer, base->frame->nb_samples, const_cast<const uint8_t **>(base->frame->extended_data), base->frame->nb_samples);
			
			last_position = base->packet->pts * av_q2d(base->stream->time_base);
			
			av_packet_unref(base->packet);
			av_frame_unref(base->frame);
		}
		
		float l = frame_read_buffer[frame_read_pos++];
		float r = frame_read_buffer[frame_read_pos++];
		
		p_buffer[pos++] = AudioFrame(l, r);
	}
}

float AudioStreamPlaybackExt::get_stream_sampling_rate() {
	if(base->loaded) {
		return float(base->codec_context->sample_rate);
	} else {
		return 0.0;
	}
}

void AudioStreamPlaybackExt::start(float p_from_pos) {
	active = true;
	seek(p_from_pos);
	//_begin_resample();
}

void AudioStreamPlaybackExt::stop() {
	active = false;
}

bool AudioStreamPlaybackExt::is_playing() const {
	return active;
}

int AudioStreamPlaybackExt::get_loop_count() const {
	return 0;
}

float AudioStreamPlaybackExt::get_playback_position() const {
	return float(last_position);
}

void AudioStreamPlaybackExt::seek(float p_time) {
	if (!active) {
		return;
	}
	
	seek_job = true;
	seek_pos = p_time;
	last_position = p_time;
}

bool AudioStreamPlaybackExt::is_buffering() const {
	// Only count as buffering if it has been happening for more than 10ms.
	return buffering && OS::get_singleton()->get_ticks_msec() - buffering_time > 10;
}

AudioStreamPlaybackExt::AudioStreamPlaybackExt() {
}

AudioStreamPlaybackExt::~AudioStreamPlaybackExt() {
	if(frame_read_buffer) {
		delete frame_read_buffer;
	}
	seek_thread.wait_to_finish();
}

void AudioStreamExt::_register_methods() {
    register_method("create", &AudioStreamExt::create);
    register_method("get_source", &AudioStreamExt::get_source);
    register_method("is_loaded", &AudioStreamExt::is_loaded);
	
	register_signal<AudioStreamExt>("loaded");
}

Ref<AudioStreamPlayback> AudioStreamExt::instance_playback() {
	Ref<AudioStreamPlaybackExt> playback;
	
	ERR_FAIL_COND_V(source.empty(), playback);
	
	playback.instance();
	playback->base = Ref<AudioStreamExt>(this);
	return playback;
}

String AudioStreamExt::get_stream_name() const {
	return "";
}

void AudioStreamExt::_run_load_job(Variant p_userdata) {
	int error;
	
	//print_line("Loading Audio Stream...");
	
	this->format_context = avformat_alloc_context();
	ERR_FAIL_COND(!this->format_context);
	
	error = avformat_open_input(&this->format_context, this->source.utf8().get_data(), NULL, NULL);
	ERR_FAIL_COND(error < 0);
	
	error = avformat_find_stream_info(this->format_context, NULL);
	ERR_FAIL_COND(error < 0);
	
	for(unsigned int i = 0; i < this->format_context->nb_streams; ++i) {
		AVStream *s = this->format_context->streams[i];
		if(s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			this->stream = s;
			break;
		}
	}
	
	ERR_FAIL_COND(!this->stream);
	
	//printf("AVSTREAM time_base %d/%d\n", stream->time_base.num, stream->time_base.den);
	//printf("AVSTREAM start_time %f\n", stream->start_time * av_q2d(stream->time_base));
	//printf("AVSTREAM duration %f\n", stream->duration * av_q2d(stream->time_base));
	
	this->codec = avcodec_find_decoder(this->stream->codecpar->codec_id);
	ERR_FAIL_COND(!this->codec);
	
	this->codec_context = avcodec_alloc_context3(this->codec);
	ERR_FAIL_COND(!this->codec_context);
	
	error = avcodec_parameters_to_context(this->codec_context, this->stream->codecpar);
	ERR_FAIL_COND(error < 0);
	
	error = avcodec_open2(this->codec_context, this->codec, NULL);
	ERR_FAIL_COND(error < 0);
	
	//printf("Codec: %s, Codec ID: %d, Bit Rate: %ld\n", codec->name, codec->id, codec_context->bit_rate);
	
	this->swr = swr_alloc();
	ERR_FAIL_COND(!this->swr);
	
	av_opt_set_int(this->swr, "in_channel_count", this->codec_context->channels, 0);
	av_opt_set_int(this->swr, "out_channel_count", this->codec_context->channels, 0);
	av_opt_set_int(this->swr, "in_channel_layout", this->codec_context->channel_layout, 0);
	av_opt_set_int(this->swr, "out_channel_layout", this->codec_context->channel_layout, 0);
	av_opt_set_int(this->swr, "in_sample_rate", this->stream->codecpar->sample_rate, 0);
	av_opt_set_int(this->swr, "out_sample_rate", this->stream->codecpar->sample_rate, 0);
	av_opt_set_sample_fmt(this->swr, "in_sample_fmt",  this->codec_context->sample_fmt, 0);
	av_opt_set_sample_fmt(this->swr, "out_sample_fmt", DESTINATION_FORMAT, 0);
	swr_init(this->swr);
	
	this->packet = av_packet_alloc();
	ERR_FAIL_COND(!this->packet);
	
	this->frame = av_frame_alloc();
	ERR_FAIL_COND(!this->frame);
	
	this->duration = this->format_context->duration * av_q2d(av_get_time_base_q());
	this->loaded = true;
	this->call_deferred("emit_signal", "loaded");
}

void AudioStreamExt::create(String p_source) {
	ERR_FAIL_COND(!source.empty());
	ERR_FAIL_COND(p_source.empty());
	
	source = p_source;
	load_thread.start(this, "_run_load_job");
}

String AudioStreamExt::get_source() const {
	return source;
}

bool AudioStreamExt::is_loaded() const {
	return loaded;
}

void AudioStreamExt::_init() {
}

AudioStreamExt::AudioStreamExt() {
}

AudioStreamExt::~AudioStreamExt() {
	if(!source.empty()) {
		load_thread.wait_to_finish();
	}
	if(format_context) {
		avformat_free_context(format_context);
	}
	if(codec_context) {
		avcodec_free_context(&codec_context);
	}
	if(swr) {
		swr_free(&swr);
	}
	if(packet) {
		av_packet_free(&packet);
	}
	if(frame) {
		av_frame_free(&frame);
	}
}
