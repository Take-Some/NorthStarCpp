////////////////////////////////////////////////////////////////////////////
//	Created 	: 29.08.2008
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_LOGGING_EXTENSIONS_H_INCLUDED
#define NORTHSTARCPP_LOGGING_EXTENSIONS_H_INCLUDED

#if NORTHSTARCPP_CORE_BUILDING
#	define NORTHSTARCPP_LOG_MODULE_INITIATOR			"core"
#endif // #if NORTHSTARCPP_CORE_BUILDING

#ifndef NORTHSTARCPP_LOG_MODULE_INITIATOR
#	error you should define NORTHSTARCPP_LOG_MODULE_INITIATOR macro before including this header
#endif // #ifndef NORTHSTARCPP_LOG_MODULE_INITIATOR

#include <fastdelegate/fastdelegate.h>

namespace northstarcpp {
namespace logging {

enum verbosity {
	silent	= 1 << 0,
	error	= 1 << 1,
	warning	= 1 << 2,
	info	= 1 << 3,
	debug	= 1 << 4,
	trace	= 1 << 5,
	unset	= 1 << 31,
}; // enum verbosity

} // namespace logging
} // namespace northstarcpp

#include <northstarcpp/compat/logging_helper.h>
#include <northstarcpp/compat/logging_settings.h>

namespace northstarcpp {
namespace logging {

enum callback_flag {
	first	= 1 << 0,
	last	= 1 << 1,
}; // enum callback_flag

typedef	fastdelegate::FastDelegate<
	void (
		pcstr,					// file
		pcstr,					// function signature
		int	 ,					// line
		pcstr,					// initiator
		int	 ,					// verbosity
		pcstr,					// log string
		callback_flag   		// first/last string
	)
>				log_callback;

NORTHSTARCPP_CORE_API	void			flush				( pcstr file_name = 0 );

NORTHSTARCPP_CORE_API	void			set_log_callback	( log_callback callback );
NORTHSTARCPP_CORE_API	log_callback	get_log_callback	( );

NORTHSTARCPP_CORE_API	void			push_rule			( pcstr initiator, int verbosity );
NORTHSTARCPP_CORE_API	void			pop_rule			( );
NORTHSTARCPP_CORE_API	void			on_thread_spawn		( );

} // namespace logging
} // namespace northstarcpp

#ifdef LOG_ERROR
#	error do not define LOG_ERROR macro
#endif // #ifdef LOG_ERROR
#ifdef LOG_WARNING
#	error do not define LOG_WARNING macro
#endif // #ifdef LOG_WARNING
#ifdef LOG_INFO
#	error do not define LOG_INFO macro
#endif // #ifdef LOG_INFO

#define LOG_ERROR( ... )				::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":", ::northstarcpp::logging::error	) ( ##__VA_ARGS__ )
#define LOG_WARNING( ... )				::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":", ::northstarcpp::logging::warning	) ( ##__VA_ARGS__ )
#define LOG_INFO( ... )					::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":", ::northstarcpp::logging::info		) ( ##__VA_ARGS__ )

#ifdef DEBUG
#	define LOG_TRACE( ... )				::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":", ::northstarcpp::logging::trace	) ( ##__VA_ARGS__ )
#	define LOGI_TRACE( initiator, ... )	::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":" initiator ":", ::northstarcpp::logging::trace) ( ##__VA_ARGS__ )
#else // #ifdef DEBUG
#	define LOG_TRACE( ... )
#	define LOGI_TRACE( ... )
#endif // #ifdef DEBUG

#ifndef NORTHSTARCPP_MASTER_GOLD
#	define LOG_DEBUG( ... )				::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":", ::northstarcpp::logging::debug	) ( ##__VA_ARGS__ )
#	define LOGI_DEBUG( initiator, ... )	::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":" initiator ":", ::northstarcpp::logging::debug) ( ##__VA_ARGS__ )
#else // #ifndef NORTHSTARCPP_MASTER_GOLD
#	define LOG_DEBUG( ... )
#	define LOGI_DEBUG( ... )
#endif // #ifndef NORTHSTARCPP_MASTER_GOLD

#define LOGI_ERROR( initiator, ... )	::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":" initiator ":", ::northstarcpp::logging::error)	( ##__VA_ARGS__ )
#define LOGI_WARNING( initiator, ... )	::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":" initiator ":", ::northstarcpp::logging::warning) ( ##__VA_ARGS__ )
#define LOGI_INFO( initiator, ... )		::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":" initiator ":", ::northstarcpp::logging::info) ( ##__VA_ARGS__ )
#define LOGI( initiator, type, ... )	::northstarcpp::logging::helper	( __FILE__, __FUNCSIG__, __LINE__, NORTHSTARCPP_LOG_MODULE_INITIATOR ":" initiator ":", type) ( ##__VA_ARGS__ )

#endif // #ifndef NORTHSTARCPP_LOGGING_EXTENSIONS_H_INCLUDED