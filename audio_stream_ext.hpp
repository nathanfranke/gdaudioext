#ifndef AUDIO_STREAM_EXT_HPP
#define AUDIO_STREAM_EXT_HPP

extern "C" {
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

#include "servers/audio/audio_stream.h"

class AudioStreamExt;

class AudioStreamPlaybackExt : public AudioStreamPlaybackResampled {
	GDCLASS(AudioStreamPlaybackExt, AudioStreamPlaybackResampled)
	friend class AudioStreamExt;
	
	int cur = 0;
	
	Ref<AudioStreamExt> base;
	bool active = false;
	
	float *frame_read_buffer = NULL;
	int frame_read_pos = 0;
	int frame_read_len = 0;
	
	bool seek_job = false;
	int64_t seek_pos;
	
	double last_position = 0.0;
	double last_duration = 0.0;
	
protected:
	virtual void _mix_internal(AudioFrame *p_buffer, int p_frames);
	virtual float get_stream_sampling_rate();
	
public:
	virtual void start(float p_from_pos = 0.0);
	virtual void stop();
	virtual bool is_playing() const;
	virtual int get_loop_count() const;
	virtual float get_playback_position() const;
	virtual void seek(float p_time);
	
	AudioStreamPlaybackExt();
	~AudioStreamPlaybackExt();
};

class AudioStreamExt : public AudioStream {
	GDCLASS(AudioStreamExt, AudioStream)
	
	friend class AudioStreamPlaybackExt;
	
	String source;
	
	float duration = 0.0;
	
	AVFormatContext *format_context;
	AVStream *stream;
	
	AVCodec *codec;
	AVCodecContext *codec_context;
	
	SwrContext *swr;
	
	AVPacket *packet;
	AVFrame *frame;
	
protected:
	static void _bind_methods();

public:
	void set_source(String p_source);
	String get_source() const;

	virtual Ref<AudioStreamPlayback> instance_playback();
	virtual String get_stream_name() const;

	virtual float get_length() const { return duration; }
	
	AudioStreamExt();
	virtual ~AudioStreamExt();
};

#endif
