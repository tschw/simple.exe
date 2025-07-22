#include "audio_output.hpp"

#include <thread>
#include <chrono>
#include <atomic>

#include <mutex>
#include <condition_variable>

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

namespace sample = audio_output_format;
using lock = std::unique_lock<std::mutex>;

class audio_output::body
{
	static constexpr int n_buffers = 4;

	ALCcontext*						ptr_context;
	ALuint							arr_buffers[n_buffers];
	ALuint							hnd_source;
	ALuint							val_index;
	AUDIO_OUTPUT_FORMAT_AL_TYPE*	arr_stream_buffer;
	sample::count					val_stream_buffer_size;

	stream_function					fnc_stream;
	std::chrono::nanoseconds		dur_buffer_playback;

	std::thread*					ptr_thread;
	std::atomic<int> volatile		enm_thread_state;
	enum thread_state				{ birth, life, death };

	std::mutex						mtx_sync;
	std::condition_variable			cnd_sync;
  public:

	body(stream_function&& func, std::chrono::milliseconds&& latency)
	  : ptr_context(0l)
	  , arr_stream_buffer(0l)
	  , val_stream_buffer_size(latency.count() *
			  	sample::rate * sample::channels / (1000 * n_buffers))
	  , fnc_stream(func)
	  , dur_buffer_playback(latency / n_buffers)
	  , ptr_thread(0l)
	  , enm_thread_state(birth)
	{
		arr_stream_buffer = new ALshort[val_stream_buffer_size];
		ptr_thread = new std::thread([this] { this->thread_main(); });
		{	lock l(mtx_sync);
			cnd_sync.wait(l,
					[this] { return this->enm_thread_state != birth; });
		}
	}

	~body()
	{
		enm_thread_state = death;
		{	lock l(mtx_sync);
			cnd_sync.notify_one();
		}

		ptr_thread->join();

		delete ptr_thread;
		delete[] arr_stream_buffer;
	}

	bool is_running() const
	{
		return enm_thread_state == life;
	}

  private:

	void thread_main()
	{
		bool ok = init_al();

		enm_thread_state = ok ? life : death;
		{	lock l(mtx_sync);
			cnd_sync.notify_one();
		}

		if (ok)
		{
			start_streaming();

			while (enm_thread_state == life)
			{
				keep_streaming();

				{	lock l(mtx_sync);
					cnd_sync.wait_for(l, dur_buffer_playback);
				}
			}
		}
		close_al();
	}

	bool init_al()
	{
		ALCdevice * device = alcOpenDevice(0l);
		if (!device) return false;

		ptr_context = alcCreateContext(device, 0);
		if (!ptr_context)
		{
			alcCloseDevice(device);
			return false;
		}
		alcMakeContextCurrent(ptr_context);

		alGetError();

		alGenBuffers(n_buffers, arr_buffers);
		if(alGetError() != AL_NO_ERROR) return false;

		alGenSources(1, & hnd_source);
		val_index = 0;
		if(alGetError() != AL_NO_ERROR) return false;

		return true;
	}

	void close_al()
	{
		if (!ptr_context) return;
		alDeleteSources(1, & hnd_source);
		alDeleteBuffers(n_buffers, arr_buffers);
		ALCdevice * device = alcGetContextsDevice(ptr_context);
		alcDestroyContext(ptr_context);
		alcCloseDevice(device);
	}

	void fill_buffers(ALuint first, ALuint n)
	{
		for (ALuint j = first, je = first+n; j < je; ++j)
		{
			fnc_stream(arr_stream_buffer, val_stream_buffer_size);

			alBufferData(arr_buffers[j],
					AUDIO_OUTPUT_FORMAT_AL_FORMAT, arr_stream_buffer,
					val_stream_buffer_size * sample::bytes_size, sample::rate);
		}
	}

	void start_streaming()
	{
		fill_buffers(0, n_buffers);
		alSourceQueueBuffers(hnd_source, n_buffers, &arr_buffers[0]);
		alSourcef(hnd_source, AL_GAIN, 1.0f);
		alListenerf(AL_GAIN, 1.0f);
		alSourcePlay(hnd_source);
	}

	void keep_streaming()
	{
		ALint p;
		alGetSourcei(hnd_source, AL_BUFFERS_PROCESSED, & p);

		unsigned l1 = p, l2 = 0, next_index = val_index + p;
		if (next_index >= n_buffers)
		{
			l2 = next_index -= n_buffers;
			l1 = n_buffers - val_index;
		}
		alSourceUnqueueBuffers(hnd_source, l1, &arr_buffers[val_index]);
		alSourceUnqueueBuffers(hnd_source, l2, &arr_buffers[0]);
		fill_buffers(val_index, l1);
		fill_buffers(0, l2);
		alSourceQueueBuffers(hnd_source, l1, &arr_buffers[val_index]);
		alSourceQueueBuffers(hnd_source, l2, &arr_buffers[0]);

		val_index = next_index;
	}
};

audio_output::audio_output(stream_function&& f,
		std::chrono::milliseconds&& latency)
  : ptr_body(new body(std::move(f), std::move(latency)))
{ }

audio_output::~audio_output() { delete ptr_body; }

bool audio_output::is_running() const { return ptr_body->is_running(); }
