#ifndef NORTHSTARCPP_UNREFERENCED_PARAMETER_H_INCLUDED
#define NORTHSTARCPP_UNREFERENCED_PARAMETER_H_INCLUDED


namespace northstarcpp {
namespace detail {
	inline void	unreferenced_parameter_helper ( ... ) {}
} // namespace detail
} // namespace northstarcpp

#define NORTHSTARCPP_UNREFERENCED_PARAMETER(parameter) \
			(void)(&parameter)

#define NORTHSTARCPP_UNREFERENCED_PARAMETERS(...) \
	if ( northstarcpp::identity(false) ) { northstarcpp::detail::unreferenced_parameter_helper(__VA_ARGS__); } else (void)0

#endif // #ifndef NORTHSTARCPP_UNREFERENCED_PARAMETER_H_INCLUDED