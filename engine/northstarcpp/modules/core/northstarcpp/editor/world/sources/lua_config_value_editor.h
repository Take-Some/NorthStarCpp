namespace northstarcpp {
namespace editor {
#	include <northstarcpp/editor/base/unmanaged_string.h>
} // namespace editor
} // namespace northstarcpp

inline void rename_cfg(northstarcpp::configs::lua_config_value* v, System::String^ new_field_id )
{
	northstarcpp::editor::unmanaged_string		s(new_field_id);
	v->rename							(s.c_str());
}

inline void set_c_string(northstarcpp::configs::lua_config_value v, System::String^ str )
{
	northstarcpp::editor::unmanaged_string		s(str);
	v									= (s.c_str());
}
