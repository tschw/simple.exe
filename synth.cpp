#include "audio_output.hpp"

#include <vector>
#include <thread>
#include <limits>

#include <cmath>
#include <cstring>
#include <cassert>
#include <cstdio>

// #define WAV_EXPORT_MIX
// #define WAV_EXPORT_TRACKS

#if defined(WAV_EXPORT_MIX) || defined(WAV_EXPORT_TRACKS)
#	include "wav_export.hpp"
#endif

// ------------ The sound synthesis stage

enum instrument_bits : int8_t
{
	voice		= 0x01,
	envelope	= 0x02,
	kick		= 0x04,
	noise		= 0x08,
	lowpass		= 0x10,
	ringmod		= 0x20
};

namespace sample = audio_output_format;

typedef std::vector<sample::type> playback_buffer;

#ifndef WAV_EXPORT_MIX
typedef playback_buffer master_buffer;
#else
typedef std::vector<float> master_buffer;
#endif

#ifndef WAV_EXPORT_TRACKS
typedef master_buffer synth_buffer;
#else
typedef std::vector<float> synth_buffer;
#endif

namespace
{
	constexpr double tr = 44100. / sample::rate; // to adapt factors to
	constexpr double rdf(double v) { return std::pow(v, tr); } // sample rate

	constexpr float track_amplitude			= 0.5f;

#define PLEASE_NO_MACROS
	constexpr float mix_amplitude			= track_amplitude *
	 std::numeric_limits<sample::type>::max PLEASE_NO_MACROS ();

	constexpr float voice_lowpass			= rdf(0.4);
	constexpr float voice_amplitude			= 0.3;

	constexpr float ringmod_freq_factor		= 2.0;
	constexpr float ringmod_phase			= 0.5;

	constexpr float noise_amplitude			= 0.66;
	constexpr float noise_lowpass			= 0.81;

	constexpr float kick_sfreq_start		= 0.04 * tr;
	constexpr float kick_sfreq_falloff		= rdf(0.9994);

//	constexpr float kick_sfreq_start		= 0.21 * tr; //0.04 * tr;
//	constexpr float kick_sfreq_falloff		= rdf(0.996); //rdf(0.9996);

//	constexpr float kick_sfreq_start		= 0.16 * tr;
//	constexpr float kick_sfreq_falloff		= rdf(0.992);

	constexpr float envelope_falloff		= rdf(0.9996);
	constexpr float envelope_off_scale		= 0.6;

	constexpr float lowpass_cutoff_final	= 0.1;
	constexpr float lowpass_cutoff_range	= 0.1;
	constexpr float lowpass_cutoff_falloff	= rdf(0.9998);
}

class synth
{
	synth_buffer&			ref_output;
	sample::count			val_offset;
	int						val_instrument;

	float					arr_lowpass[4][4];

  public:

	synth(synth_buffer& output, sample::count offset, unsigned instrument)
	  : ref_output(output), val_offset(offset), val_instrument(instrument)
	{
		std::memset(arr_lowpass, 0, sizeof(arr_lowpass));
	}


	void note_on(unsigned note, sample::count n)
	{
		// envelope (env. value and falloff factor)
		float env = 1.0f, lowpass_cutoff_env = 1.0;
		// kick (sine arg, delta and falloff factor)
		float kx = 0.0f;
		float dkx = kick_sfreq_start;
		float dkxf = kick_sfreq_falloff;
		// tone generator (frequency)
		float f = get_note_frequency(note);

		for (int i = 0; i < n; ++i)
		{
			float s = 0.0f, t = (float) i * f;
			s += get_prop(kick) * std::sin(kx), kx += (dkx *= dkxf);
			s += get_prop(noise) * noise_amplitude *
					lp(0, osc_noise(), noise_lowpass);

			s += get_prop(voice, voice_amplitude) * lp(1, osc_saw(t) *
					get_prop(ringmod,
						osc_rect(ringmod_phase +
							t * ringmod_freq_factor), 1.0f),
					get_prop(lowpass, 1.0, voice_lowpass));

			if (!! (val_instrument & lowpass))
			{
				s = lp(2, s, lowpass_cutoff_final +
						lowpass_cutoff_range * lowpass_cutoff_env);

				lowpass_cutoff_env *= lowpass_cutoff_falloff;
			}

			s *= get_prop(envelope, env *=
					envelope_falloff, envelope_off_scale);

#if !defined(WAV_EXPORT_MIX) && !defined(WAV_EXPORT_TRACKS)
			ref_output[val_offset++] += sample::type(s * mix_amplitude);
#elif !defined(WAV_EXPORT_TRACKS)
			ref_output[val_offset++] += s * track_amplitude;
#else
			ref_output[val_offset++] += s;
#endif
		}
	}

  private:

	float get_prop(unsigned bits, float val = 1.0f, float nval = 0.0f)
	{ return val_instrument & bits ? val : nval;  }

	static float osc_noise()
	{ return (( (float)random()/(float)RAND_MAX )-0.5f)*2.0f; }

	static float osc_saw(float x)
	{ return fmod(x, 2.0) - 1.0f; }

	static float osc_rect(float x)
	{ return sin(x * M_PI) < 0.0f ? -1.0f : 1.0f; }

	static float ctrl_osc_sin(float x)
	{ return 0.5f - 0.5f * std::cos(x); }

	float lp(int i, float in, float f)
	{
		float* s = arr_lowpass[i];
		s[0] += (in - s[0]) * f;
		s[1] += (s[0] - s[1]) * f;
		s[2] += (s[1] - s[2]) * f;
		s[3] += (s[2] - s[3]) * f;
		return s[3];
	}

	static float get_note_frequency(unsigned note)
	{
		static float const tuning = 32.75f / float(sample::rate); 
		return tuning *
				std::exp2(double(note / 12)) *
				std::pow(1.05946309435929530984311, note % 12);
	}
};

// ------------ The sequencer (uses and controls the synth)

enum sequence_termination : int8_t
{
	end = -127,
	also_place_at = -128
};

namespace sequencer
{
	namespace {

		using sample::count;

		constexpr count samples_per_minute = sample::rate * 60;

		constexpr count bars_total = 61;
		constexpr count beats_per_bar = 4;
		constexpr count beats_per_minute = 120;
		constexpr count units_per_bar = 16;

		constexpr count samples_per_unit =
		   (samples_per_minute * beats_per_bar)
		   / (beats_per_minute * units_per_bar);

		constexpr count samples_per_beat =
		   samples_per_unit * units_per_bar / beats_per_bar;

		constexpr count samples_per_bar = samples_per_beat * beats_per_bar;

		constexpr count length_in_samples = bars_total * samples_per_bar;

		int8_t const music[] = {
		// bass, half
			voice | lowpass, 0, 0, 1,
			15,1,	13,
			-8,2,	3,2,
			24,1,	-5,
			also_place_at, 44, 1, 52, 1, 0,
		// bass, full
			voice | lowpass, 0, 4, 1,
			15,1,	13,
			-8,6,	3,2,-5,6,-3,-7,
			also_place_at, 32, 3, 48, 1, 56, 1, 0,
		// bass, last note
			voice | lowpass, 0, 60, 1,
			15,1,	13,
			end,
		// synth 1
			voice | ringmod | envelope, 0, 8, 18,
			1,1,   13,
		   -1,3,   12,-12,13,
			1,2,   -14,19,
		   -1,1,   18,
			1,4,   -37,25,-24,3,
		   -1,2,   24,-10,
			2,1,   -2,
		   -1,1,   -10,
		   -1,1,   10,
			1,1,   -10,
		   -1,4,   48,-10,-12,-12,
			end,
		// kick, ground beat
			kick | envelope, 0, 16, 4,
			3,16,
			also_place_at, 36, 4, 0,
		// kick, syncopation
			kick | envelope, 11, 16, 4,
			63,1,
			also_place_at, 36, 4, 0,
		// snare
			noise | envelope, 4, 20, 3,
			6,1,
			2,1,
		   -1,1,
		   -6,1,
			4,1,
			2,2,
		   -7,1,
		   14,1,
			also_place_at, 44, 2, 0,
		// synth 2
			voice | ringmod | envelope, 0, 24, 2,
			0,1,	64,
			1,1,	1,
		   -1,2,	-3,0,
			4,1,	0,
		   -1,1,	6,
		   -2,8,	0,0,-1,0,-3,0,-1,0,
			4,1,	-7,
		   -5,1,	0,
			1,1,	0,
		   23,1,	-7,
		   also_place_at, 36, 2, 0
		};
	}

	template< std::size_t N_Bytes >
	void render(master_buffer& master, int8_t const (& data)[N_Bytes])
	{
		// iterate sequences
		int8_t const* in_pos = data;
		int8_t const* in_end = data + N_Bytes;
#ifndef WAV_EXPORT_TRACKS
		synth_buffer& mix = master;
#else
		synth_buffer mix(length_in_samples);
		int track = 1;
#endif
		while (in_pos < in_end)
		{
			// read sequence header
			int		instrument	= *in_pos++;
			count	placement_u = *in_pos++;
			count	placement_b = *in_pos++;

			int8_t const* segments_start = in_pos + 1;
			int8_t const* read;

			do
			{
				int loop = *in_pos++;

				// create synth
				synth s(mix, placement_b * samples_per_bar +
						placement_u * samples_per_unit, instrument);

				// repeat sequence times "loop"
				for (int i = loop; --i >= 0 ;)
				{
					read = segments_start;
					// process all segments in the sequence
					unsigned note = 0, note_length = 1;
					for (int note_len_delta;
							(note_len_delta = *read) > end;
							++read)
					{
						note_length += note_len_delta;
						size_t notes_in_segment = *++read;

						// process all notes in the segment
						for (int j = notes_in_segment; j > 0; --j)
						{
							if (!!(instrument & voice))
								note += *++read;

							s.note_on(note,
									note_length * samples_per_unit);
						}
					}
				} // loop

				if (in_pos < read)
					in_pos = read + 1;

			} while (*read == also_place_at && !!(placement_b = *in_pos++));
#ifdef WAV_EXPORT_TRACKS
			if (in_pos >= in_end || *in_pos != instrument)
			{
				char path[64];
				if (int(sizeof(path)) > snprintf(
						path, sizeof(path), "track_%02d.wav", track++))
					wav_export(path, mix.data(), mix.size(), sample::rate);

				for (count i = 0; i < length_in_samples; ++i)
#	ifndef WAV_EXPORT_MIX
					master[i] += mix[i] * synth::mix_amplitude;
#	else
					master[i] += mix[i] * synth::track_amplitude;
#	endif
				if (in_pos < in_end)
					std::memset(mix.data(), 0,
							length_in_samples * sizeof(float));
			}
#endif
		}
	}
}

class player
{
	playback_buffer			buf_play;
	audio_output*			ptr_device;
	sample::count			val_play_offset;

  public:

	player()
	  : buf_play(sequencer::length_in_samples),
	  	ptr_device(0l),
		val_play_offset(0)
	{
		using namespace sequencer;

#ifndef WAV_EXPORT_MIX
		render(buf_play, music);
#else
#	define PLEASE_NO_MACROS
		master_buffer master(length_in_samples);
		render(master, music);
		wav_export("mix.wav", master.data(), master.size(), sample::rate);
		for (sample::count i = 0; i < length_in_samples; ++i)
			buf_play[i] = sample::type(master[i] *
					std::numeric_limits<sample::type>::max PLEASE_NO_MACROS ());

#endif
	}

	~player() { stop(); }

	void play()
	{
		using namespace sample;

		if (! ptr_device)
		{
			val_play_offset = 0;
			ptr_device = new audio_output(
					[this](type* b, count n) { this->stream(b, n); } );
		}
	}

	void stop()
	{
		delete ptr_device;
		ptr_device = 0l;
	}

	void play_all()
	{
		play();
		auto duration = std::chrono::microseconds(
				1000000ll * sequencer::length_in_samples / sample::rate);
		std::this_thread::sleep_for(duration);
		stop();
	}

  private:

	player(player const&) = delete;
	player& operator=(player const&) = delete;

	void stream(sample::type* to_device, sample::count n)
	{
		std::memcpy(to_device,
				buf_play.data() + val_play_offset, n * sample::bytes_size);

		val_play_offset += n;

		if (val_play_offset >= sample::count(buf_play.size()))
			val_play_offset = 0;
	}
};

// ------------ Main program

int main(int argc, char const* argv[])
{
	using std::printf;

	printf("Rendering music - please wait...\n");
	player p;

	printf("Now playing...\n");
	p.play_all();

	printf("\nPlayback complete - shutting down.\n");
	return 0;
}

