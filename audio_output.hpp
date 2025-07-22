#ifndef AUDIO_OUTPUT_HPP_INCLUDED
#define AUDIO_OUTPUT_HPP_INCLUDED

#include "audio_output_format.hpp"

#include <chrono>
#include <functional>

using std::literals::chrono_literals::operator""ms;

class audio_output
{
    class body; body* ptr_body;

	typedef void stream_function_signature(
			audio_output_format::type*, audio_output_format::count);

    typedef std::function<stream_function_signature> stream_function;
  public:

    explicit audio_output(stream_function&&,
			std::chrono::milliseconds&& latency = 16ms);

    ~audio_output();

	bool is_running() const;

  private:
    audio_output(audio_output const&) = delete;
    audio_output& operator=(audio_output const&) = delete;
};

#endif
