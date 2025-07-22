#include "wav_export.hpp"

#include <bit>

#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

namespace {

	template< std::size_t N >
	void append_tag(u8*& d, char const (&id)[N])
	{
		std::memcpy(d, &id, N-1);
		d += N-1;
	}

	template< typename T, std::endian ByteOrder >
	void append(u8*& d, T v)
	{
		if constexpr (ByteOrder != std::endian::native) v = std::byteswap(v);
		std::memcpy(d, &v, sizeof(T));
		d += sizeof(T);
	}

	template< typename T >
	bool swap_and_write(u8 const* src, std::size_t n_bytes, std::FILE* f)
	{
		std::size_t n = (n_bytes + sizeof(T) - 1) / sizeof(T);
		T* buf = new T[n];
		T* dst = buf;
		for (std::size_t i = 0; i < n; ++i, src += sizeof(T))
		{
			T tmp;
			std::memcpy(&tmp, src, sizeof(tmp));
			*dst++ = std::byteswap(tmp);
		}
		bool ok = std::fwrite(buf, n_bytes, 1, f) == 1;
		delete[] buf;
		return ok;
	}

	template <std::endian ByteOrder >
	bool write_buffer(
			u8 const* data, std::size_t size, u8 word_size, std::FILE* f)
	{
		if constexpr (ByteOrder != std::endian::native)
		{
			switch (word_size)
			{
				case 2: return swap_and_write<u16>(data, size, f);
				case 4: return swap_and_write<u32>(data, size, f);
				case 8: return swap_and_write<u64>(data, size, f);
				case 1: break;

				default: assert(!!! "unsupported");
			}
		}
		return std::fwrite(data, size, 1, f) == 1;
	}
}

bool wav_export(
		char const* path, void const* data, wav_format::bytes_count size,
		wav_format::sample_rate r, wav_format::channels c, wav_format::type t)
{
	auto constexpr le = std::endian::little;

	u8 header[44];
	u8* d = & header[0];
	append_tag(d, "RIFF");
	append<u32,le>(d, sizeof(header) + size - 8);
	append_tag(d, "WAVEfmt ");
	append<u32,le>(d, 0x10);
	enum fmt : u16 { ints = 1, floats = 3 };
	append<u16,le>(d, ! (t & 0x80) ? ints : floats);
	u16 s = u16(t & 0x7f);
	append<u16,le>(d, c);
	append<u32,le>(d, r);
	append<u32,le>(d, r * s * c);
	append<u16,le>(d, s * c);
	append<u16,le>(d, s * 8);
	append_tag(d, "data");
	append<u32,le>(d, size);
	assert(d == & header[sizeof(header)]);

	std::FILE* f = fopen(path, "wb");
	if (! f) return false;
	bool ok = true;
	if ((ok = std::fwrite(& header, sizeof(header), 1, f) == 1))
		ok = write_buffer<le>(static_cast<u8 const*>(data), size, s, f);
	std::fclose(f);
	return ok;
}
