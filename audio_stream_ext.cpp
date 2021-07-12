#include "audio_stream_ext.hpp"

void AudioStreamPlaybackExt::_mix_internal(AudioFrame *p_buffer, int p_frames) {
	ERR_FAIL_COND(!active);
	int error;
	
	if(seek_job) {
		seek_job = false;
		
		error = avformat_seek_file(base->format_context, base->stream->index, INT64_MIN, seek_pos, INT64_MAX, 0);
		ERR_FAIL_COND_MSG(error < 0, "Failed to seek.");
	}
	
	int pos = 0;
	
	while(pos < p_frames) {
		if(frame_read_pos >= frame_read_len) {
			if(frame_read_buffer) {
				AudioServer::get_singleton()->audio_data_free(frame_read_buffer);
			}
			
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
			
			frame_read_buffer = (float *)AudioServer::get_singleton()->audio_data_alloc(sizeof(float) * base->frame->nb_samples * 2); // TODO: Assuming two channels
			frame_read_pos = 0;
			frame_read_len = base->frame->nb_samples * 2;
			
			swr_convert(base->swr, (uint8_t **) &frame_read_buffer, base->frame->nb_samples, const_cast<const uint8_t **>(base->frame->extended_data), base->frame->nb_samples);
			
			last_position = base->packet->pts * av_q2d(base->stream->time_base);
			last_duration = base->stream->duration * av_q2d(base->stream->time_base);
			
			av_packet_unref(base->packet);
			av_frame_unref(base->frame);
		}
		
		float l = frame_read_buffer[frame_read_pos++];
		float r = frame_read_buffer[frame_read_pos++];
		
		p_buffer[pos++] = AudioFrame(l, r);
	}
}

float AudioStreamPlaybackExt::get_stream_sampling_rate() {
	return float(1.0 / av_q2d(base->stream->time_base));
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

	if (p_time >= last_duration) {
		p_time = 0;
	}

	seek_job = true;
	seek_pos = int64_t(p_time / av_q2d(base->stream->time_base));
}

AudioStreamPlaybackExt::AudioStreamPlaybackExt() {
}

AudioStreamPlaybackExt::~AudioStreamPlaybackExt() {
	if(frame_read_buffer) {
		AudioServer::get_singleton()->audio_data_free(frame_read_buffer);
	}
}

Ref<AudioStreamPlayback> AudioStreamExt::instance_playback() {
	Ref<AudioStreamPlaybackExt> playback;
	
	ERR_FAIL_COND_V_MSG(source.empty(), playback, "No source specified. Please set the 'source' variable on the stream.");
	
	playback.instance();
	playback->base = Ref<AudioStreamExt>(this);
	return playback;
}

String AudioStreamExt::get_stream_name() const {
	return "";
}

void AudioStreamExt::set_source(String p_source) {
	if(!source.empty()) {
		duration = 0.0;
		avformat_free_context(format_context);
		avcodec_free_context(&codec_context);
		swr_free(&swr);
		av_packet_free(&packet);
		av_frame_free(&frame);
	}
	
	if(!p_source.empty()) {
		int error;
		
		//print_line("Loading Audio Stream...");
		
		AVFormatContext *format_context = avformat_alloc_context();
		ERR_FAIL_COND_MSG(!format_context, "Failed to allocate AVFormatContext.");
		
		error = avformat_open_input(&format_context, p_source.utf8().ptrw(), NULL, NULL);
		ERR_FAIL_COND_MSG(error < 0, "Failed to open input file '" + p_source + "'.");
		
		error = avformat_find_stream_info(format_context, NULL);
		ERR_FAIL_COND_MSG(error < 0, "Failed to find stream info.");
		
		AVStream *stream = NULL;
		for(unsigned int i = 0; i < format_context->nb_streams; ++i) {
			AVStream *s = format_context->streams[i];
			if(s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
				stream = s;
				break;
			}
		}
		
		ERR_FAIL_COND_MSG(!stream, "Failed to find an audio stream.");
		
		//printf("AVSTREAM time_base %d/%d\n", stream->time_base.num, stream->time_base.den);
		//printf("AVSTREAM start_time %f\n", stream->start_time * av_q2d(stream->time_base));
		//printf("AVSTREAM duration %f\n", stream->duration * av_q2d(stream->time_base));
		
		duration = stream->duration * av_q2d(stream->time_base);
		
		AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
		ERR_FAIL_COND_MSG(!codec, "Unsupported codec: " + String(Variant(stream->codecpar->codec_id)) + ".");
		
		AVCodecContext *codec_context = avcodec_alloc_context3(codec);
		ERR_FAIL_COND_MSG(!codec_context, "Failed to allocate AVCodecContext.");
		
		error = avcodec_open2(codec_context, codec, NULL);
		ERR_FAIL_COND_MSG(error < 0, "Failed to open codec.");
		
		//printf("Codec: %s, Codec ID: %d, Bit Rate: %ld\n", codec->name, codec->id, codec_context->bit_rate);
		
		SwrContext *swr = swr_alloc();
		ERR_FAIL_COND_MSG(!swr, "Failed to allocate SwrContext.");
		
		av_opt_set_int(swr, "in_channel_layout", stream->codecpar->channel_layout, 0);
		av_opt_set_int(swr, "out_channel_layout", stream->codecpar->channel_layout, 0);
		av_opt_set_int(swr, "in_sample_rate", stream->codecpar->sample_rate, 0);
		av_opt_set_int(swr, "out_sample_rate", stream->codecpar->sample_rate, 0);
		av_opt_set_sample_fmt(swr, "in_sample_fmt",  AV_SAMPLE_FMT_FLTP, 0);
		av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
		swr_init(swr);
		
		AVPacket *packet = av_packet_alloc();
		ERR_FAIL_COND_MSG(!packet, "Failed to allocate AVPacket.");
		
		AVFrame *frame = av_frame_alloc();
		ERR_FAIL_COND_MSG(!frame, "Failed to allocate AVFrame.");
		
		this->format_context = format_context;
		this->stream = stream;
		this->codec = codec;
		this->codec_context = codec_context;
		this->swr = swr;
		this->packet = packet;
		this->frame = frame;
	}
	
	this->source = p_source;
}

String AudioStreamExt::get_source() const {
	return source;
}

void AudioStreamExt::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_source", "source"), &AudioStreamExt::set_source);
	ClassDB::bind_method(D_METHOD("get_source"), &AudioStreamExt::get_source);
	
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR), "set_source", "get_source");
}

AudioStreamExt::AudioStreamExt() {
}

AudioStreamExt::~AudioStreamExt() {
	source = "";
}
