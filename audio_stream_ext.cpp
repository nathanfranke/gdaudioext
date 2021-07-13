#include "audio_stream_ext.hpp"

void AudioStreamPlaybackExt::_run_seek_job(void *p_self) {
	AudioStreamPlaybackExt *self = (AudioStreamPlaybackExt *)p_self;
	
	self->busy_seeking_time = OS::get_singleton()->get_ticks_msec();
	self->busy_seeking = true;
	
	self->base->load_thread.wait_to_finish();
	
	if(self->base->loaded) {
		int64_t frame = int64_t(self->seek_pos / av_q2d(self->base->stream->time_base));
		
		// This call can block for several hundred milliseconds if audio from the internet is playing.
		int error = avformat_seek_file(self->base->format_context, self->base->stream->index, INT64_MIN, frame, INT64_MAX, AVSEEK_FLAG_ANY);
		ERR_FAIL_COND_MSG(error < 0, "Failed to seek.");
		
		self->busy_seeking = false;
	}
}

void AudioStreamPlaybackExt::_clear_frame_buffer() {
	if(frame_read_buffer) {
		AudioServer::get_singleton()->audio_data_free(frame_read_buffer);
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
		
		// Clear old frame buffer so only fresh audio plays.
		_clear_frame_buffer();
		
		// Play silence while loading.
		while(pos < p_frames) {
			p_buffer[pos++] = AudioFrame(0, 0);
		}
		
		seek_thread.start(AudioStreamPlaybackExt::_run_seek_job, this);
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
			frame_read_buffer = (float *)AudioServer::get_singleton()->audio_data_alloc(alloc_size);
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
	_begin_resample();
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
	return busy_seeking && OS::get_singleton()->get_ticks_msec() - busy_seeking_time > 10;
}

void AudioStreamPlaybackExt::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_buffering"), &AudioStreamPlaybackExt::is_buffering);
}

AudioStreamPlaybackExt::AudioStreamPlaybackExt() {
}

AudioStreamPlaybackExt::~AudioStreamPlaybackExt() {
	if(frame_read_buffer) {
		AudioServer::get_singleton()->audio_data_free(frame_read_buffer);
	}
	seek_thread.wait_to_finish();
}

Ref<AudioStreamPlayback> AudioStreamExt::instance_playback() {
	Ref<AudioStreamPlaybackExt> playback;
	
	ERR_FAIL_COND_V_MSG(source.empty(), playback, "No source specified. Please call the 'create' method.");
	
	playback.instance();
	playback->base = Ref<AudioStreamExt>(this);
	return playback;
}

String AudioStreamExt::get_stream_name() const {
	return "";
}

void AudioStreamExt::_run_load_job(void *p_self) {
	AudioStreamExt *self = (AudioStreamExt *) p_self;
	
	int error;
	
	//print_line("Loading Audio Stream...");
	
	self->format_context = avformat_alloc_context();
	ERR_FAIL_COND_MSG(!self->format_context, "Failed to allocate AVFormatContext.");
	
	error = avformat_open_input(&self->format_context, self->source.utf8().ptrw(), NULL, NULL);
	ERR_FAIL_COND_MSG(error < 0, "Failed to open input file '" + self->source + "'.");
	
	error = avformat_find_stream_info(self->format_context, NULL);
	ERR_FAIL_COND_MSG(error < 0, "Failed to find stream info.");
	
	for(unsigned int i = 0; i < self->format_context->nb_streams; ++i) {
		AVStream *s = self->format_context->streams[i];
		if(s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			self->stream = s;
			break;
		}
	}
	
	ERR_FAIL_COND_MSG(!self->stream, "Failed to find an audio stream.");
	
	//printf("AVSTREAM time_base %d/%d\n", stream->time_base.num, stream->time_base.den);
	//printf("AVSTREAM start_time %f\n", stream->start_time * av_q2d(stream->time_base));
	//printf("AVSTREAM duration %f\n", stream->duration * av_q2d(stream->time_base));
	
	self->codec = avcodec_find_decoder(self->stream->codecpar->codec_id);
	ERR_FAIL_COND_MSG(!self->codec, "Unsupported codec: " + String(Variant(self->stream->codecpar->codec_id)) + ".");
	
	self->codec_context = avcodec_alloc_context3(self->codec);
	ERR_FAIL_COND_MSG(!self->codec_context, "Failed to allocate AVCodecContext.");
	
	error = avcodec_parameters_to_context(self->codec_context, self->stream->codecpar);
	ERR_FAIL_COND_MSG(error < 0, "Failed to copy parameters to context.");
	
	error = avcodec_open2(self->codec_context, self->codec, NULL);
	ERR_FAIL_COND_MSG(error < 0, "Failed to open codec.");
	
	//printf("Codec: %s, Codec ID: %d, Bit Rate: %ld\n", codec->name, codec->id, codec_context->bit_rate);
	
	self->swr = swr_alloc();
	ERR_FAIL_COND_MSG(!self->swr, "Failed to allocate SwrContext.");
	
	av_opt_set_int(self->swr, "in_channel_count", self->codec_context->channels, 0);
	av_opt_set_int(self->swr, "out_channel_count", self->codec_context->channels, 0);
	av_opt_set_int(self->swr, "in_channel_layout", self->codec_context->channel_layout, 0);
	av_opt_set_int(self->swr, "out_channel_layout", self->codec_context->channel_layout, 0);
	av_opt_set_int(self->swr, "in_sample_rate", self->stream->codecpar->sample_rate, 0);
	av_opt_set_int(self->swr, "out_sample_rate", self->stream->codecpar->sample_rate, 0);
	av_opt_set_sample_fmt(self->swr, "in_sample_fmt",  self->codec_context->sample_fmt, 0);
	av_opt_set_sample_fmt(self->swr, "out_sample_fmt", DESTINATION_FORMAT, 0);
	swr_init(self->swr);
	
	self->packet = av_packet_alloc();
	ERR_FAIL_COND_MSG(!self->packet, "Failed to allocate AVPacket.");
	
	self->frame = av_frame_alloc();
	ERR_FAIL_COND_MSG(!self->frame, "Failed to allocate AVFrame.");
	
	self->duration = self->format_context->duration * av_q2d(AV_TIME_BASE_Q);
	self->loaded = true;
	self->call_deferred("emit_signal", "loaded");
}

void AudioStreamExt::create(String p_source) {
	ERR_FAIL_COND_MSG(!source.empty(), "Stream has already been created.");
	ERR_FAIL_COND_MSG(p_source.empty(), "Invalid source specified.");
	
	source = p_source;
	load_thread.start(AudioStreamExt::_run_load_job, this);
}

String AudioStreamExt::get_source() const {
	return source;
}

bool AudioStreamExt::is_loaded() const {
	return loaded;
}

void AudioStreamExt::_bind_methods() {
	ClassDB::bind_method(D_METHOD("create", "source"), &AudioStreamExt::create);
	ClassDB::bind_method(D_METHOD("get_source"), &AudioStreamExt::get_source);
	ClassDB::bind_method(D_METHOD("is_loaded"), &AudioStreamExt::is_loaded);
	
	ADD_SIGNAL(MethodInfo("loaded"));
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
