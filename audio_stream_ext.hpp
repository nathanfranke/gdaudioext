#ifndef AUDIO_STREAM_EXT_HPP
#define AUDIO_STREAM_EXT_HPP

extern "C" {
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

#include "core/os/thread.h"
#include "servers/audio/audio_stream.h"

class AudioStreamExt;

class AudioStreamPlaybackExt : public AudioStreamPlaybackResampled {
	GDCLASS(AudioStreamPlaybackExt, AudioStreamPlaybackResampled)
	friend class AudioStreamExt;
	
	Ref<AudioStreamExt> base;
	bool active = false;
	
	float *frame_read_buffer = NULL;
	int frame_read_pos = 0;
	int frame_read_len = 0;
	
	bool busy_seeking = false;
	
	int64_t buffering_time = INT64_MIN;
	bool buffering = true;
	
	bool seek_job = false;
	float seek_pos;
	
	Thread seek_thread;
	static void _run_seek_job(void *p_self);
	
	double last_position = 0.0;
	
protected:
	void _clear_frame_buffer();
	virtual void _mix_internal(AudioFrame *p_buffer, int p_frames);
	virtual float get_stream_sampling_rate();
	
	static void _bind_methods();
	
public:
	virtual void start(float p_from_pos = 0.0);
	virtual void stop();
	virtual bool is_playing() const;
	virtual int get_loop_count() const;
	virtual float get_playback_position() const;
	virtual void seek(float p_time);
	
	virtual bool is_buffering() const;
	
	AudioStreamPlaybackExt();
	~AudioStreamPlaybackExt();
};

class AudioStreamExt : public AudioStream {
	GDCLASS(AudioStreamExt, AudioStream)
	
	friend class AudioStreamPlaybackExt;
	
	static const AVSampleFormat DESTINATION_FORMAT = AV_SAMPLE_FMT_FLT;
	
	String source;
	float duration = 0.0;
	
	AVFormatContext *format_context = NULL;
	AVStream *stream = NULL;
	AVCodec *codec = NULL;
	AVCodecContext *codec_context = NULL;
	SwrContext *swr = NULL;
	AVPacket *packet = NULL;
	AVFrame *frame = NULL;
	
	bool loaded = false;
	Thread load_thread;
	static void _run_load_job(void *p_self);
	
protected:
	static void _bind_methods();
	
public:
	void create(String p_source);
	String get_source() const;
	bool is_loaded() const;
	
	virtual Ref<AudioStreamPlayback> instance_playback();
	virtual String get_stream_name() const;
	
	virtual float get_length() const { return duration; }
	
	AudioStreamExt();
	virtual ~AudioStreamExt();
};

#endif
