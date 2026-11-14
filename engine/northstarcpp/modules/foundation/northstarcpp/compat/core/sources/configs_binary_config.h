////////////////////////////////////////////////////////////////////////////
//	Created		: 11.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef CONFIG_READER_H_INCLUDED
#define CONFIG_READER_H_INCLUDED

#include <northstarcpp/compat/configs.h>

namespace northstarcpp {

namespace resources {
	class queries_result;
} // namespace resources

namespace core {
namespace configs {

class binary_config : public northstarcpp::configs::binary_config {
public:
					binary_config	( pcbyte buffer, u32 buffer_size, northstarcpp::memory::base_allocator* allocator );
	virtual		   ~binary_config	( );

private:
//			bool	load_resource	( resources::queries_result& data );
			void	load			( pcbyte buffer, u32 buffer_size );

private:
	northstarcpp::memory::base_allocator*			m_allocator;
};

} // namespace configs
} // namespace core
} // namespace northstarcpp

#endif // #ifndef CONFIG_READER_H_INCLUDED