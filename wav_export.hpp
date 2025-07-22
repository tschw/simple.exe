#ifndef WAV_EXPORT_HPP_INCLUDED
#define WAV_EXPORT_HPP_INCLUDED

#include <cstdint>
#include <type_traits>

namespace wav_format {

	typedef std::uint32_t bytes_count;
	typedef std::uint32_t sample_rate;
	typedef std::int32_t sample_count;

	enum channels { mono = 1, stereo = 2 };
	enum type { i8 = 1, i16 = 2, i32 = 4, f32 = 0x84, f64 = 0x88 };

	template< typename T > struct detect_type
		: std::integral_constant< type, static_cast<type>(
				sizeof(T) | std::is_floating_point<T>() * 0x80)>
	{
		static_assert(std::is_arithmetic<T>() && (
				std::is_signed<T>() || sizeof(T) == 1 ));
	};
}

bool wav_export(
		char const* path, void const* data, wav_format::bytes_count,
		wav_format::sample_rate, wav_format::channels, wav_format::type);

template< typename T >
static inline bool wav_export(
		char const* path, T const* data,
		wav_format::sample_count n,
		wav_format::sample_rate r,
		wav_format::channels c = wav_format::mono)
{
	return n >= 0 && wav_export( path,
			data, n * sizeof(T), r, c, wav_format::detect_type<T>() );
}

#endif

