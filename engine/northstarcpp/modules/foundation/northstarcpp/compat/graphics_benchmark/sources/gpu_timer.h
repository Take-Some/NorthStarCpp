////////////////////////////////////////////////////////////////////////////
//	Created		: 11.06.2010
//	Author		: Nikolay Partas
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef GPU_TIMER_H_INCLUDED
#define GPU_TIMER_H_INCLUDED



namespace northstarcpp {
namespace graphics_benchmark {

class benchmark;

class gpu_timer: public boost::noncopyable
{
public:
	gpu_timer(/*benchmark&*/);
	~gpu_timer();

	void start();
	void stop();
	
	// Return time in seconds.
	double get_elapsed_time() const;

	// Return elapsed ticks.
	u64 get_elapsed_ticks() const;

	// Return elapsed seconds and ticks.
	void get_elapsed_info(double& out_sec, u64& out_ticks) const;

private:
	ID3DQuery *m_start_time,
				*m_stop_time,
				*m_freq_info;
};

} // namespace graphics_benchmark
} // namespace northstarcpp

#endif // #ifndef GPU_TIMER_H_INCLUDED