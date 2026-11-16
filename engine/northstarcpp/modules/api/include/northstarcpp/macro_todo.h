#ifndef NORTHSTARCPP_MACRO_TODO_H_INCLUDED
#define NORTHSTARCPP_MACRO_TODO_H_INCLUDED

#ifdef NORTHSTARCPP_TODO
#	error do not define NORTHSTARCPP_TODO macro
#endif // #ifdef NORTHSTARCPP_TODO

#define NORTHSTARCPP_TODO(message_to_show)	__FILE__ "(" NORTHSTARCPP_MAKE_STRING(__LINE__) "): @todo: " message_to_show

#if defined(GCC_VERSION) && ( GCC_VERSION < 4 )
	// to disable unknown pragma warning message
#	pragma warning( disable : ??? )
#endif // #if defined(GCC_VERSION) && ( GCC_VERSION < 4.5 )

#endif // #ifndef NORTHSTARCPP_MACRO_TODO_H_INCLUDED