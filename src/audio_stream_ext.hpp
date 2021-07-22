#ifndef AUDIO_STREAM_EXT_HPP
#define AUDIO_STREAM_EXT_HPP

extern "C" {
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

#include <Godot.hpp>
#include <AudioStreamPlaybackResampled.hpp>
#include <AudioStream.hpp>
#include <Thread.hpp>

struct AudioFrame {
	float l, r;
	
	AudioFrame(float l, float r) : l(l), r(r) {}
};

namespace godot {
	class AudioStreamExt;

	class AudioStreamPlaybackExt : public AudioStreamPlaybackResampled {
		GODOT_CLASS(AudioStreamPlaybackExt, AudioStreamPlaybackResampled)
		
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
		
		double last_position = 0.0;
		
	protected:
		void _clear_frame_buffer();
		virtual void _mix_internal(AudioFrame *p_buffer, int p_frames);
		virtual float get_stream_sampling_rate();
		
	public:
		static void _register_methods();
		
		virtual void start(float p_from_pos = 0.0);
		virtual void stop();
		virtual bool is_playing() const;
		virtual int get_loop_count() const;
		virtual float get_playback_position() const;
		virtual void seek(float p_time);
		
		virtual bool is_buffering() const;
		
		void _run_seek_job(Variant p_userdata);
		
		AudioStreamPlaybackExt();
		~AudioStreamPlaybackExt();
	};

	class AudioStreamExt : public AudioStream {
		GODOT_CLASS(AudioStreamExt, AudioStream)
		
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
		
	public:
		static void _register_methods();
		
		void create(String p_source);
		String get_source() const;
		bool is_loaded() const;
		
		virtual Ref<AudioStreamPlayback> instance_playback();
		virtual String get_stream_name() const;
		
		virtual float get_length() const { return duration; }
		
		void _run_load_job(Variant p_userdata);
		
		void _init();
		
		AudioStreamExt();
		virtual ~AudioStreamExt();
	};
}

#endif
