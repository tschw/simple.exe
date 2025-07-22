#ifndef AUDIO_OUTPUT_FORMAT_HPP_INCLUDED
#define AUDIO_OUTPUT_FORMAT_HPP_INCLUDED

#include <cstdint>

#define AUDIO_OUTPUT_FORMAT_AL_TYPE		ALshort
#define AUDIO_OUTPUT_FORMAT_AL_FORMAT	AL_FORMAT_MONO16

namespace audio_output_format {

	typedef		std::int16_t 		type;
	static constexpr std::int32_t	rate = 192000;
	static constexpr std::int8_t	channels = 1;
	static constexpr std::int8_t	bytes_size = sizeof(type);

	typedef		int					count;
}

#endif
