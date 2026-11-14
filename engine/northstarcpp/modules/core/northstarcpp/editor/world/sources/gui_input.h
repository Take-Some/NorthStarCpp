////////////////////////////////////////////////////////////////////////////
//	Created		: 13.02.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef GUI_INPUT_H_INCLUDED
#define GUI_INPUT_H_INCLUDED


namespace northstarcpp {
namespace editor {


public ref class gui_input_ abstract{
public:

	virtual System::Collections::ArrayList^			get_held_keys				( );
	virtual System::Windows::Forms::MouseButtons	get_mouse_buttons			( );
	virtual System::Drawing::Point					get_mouse_position			( );
	virtual System::Drawing::Size					get_view_size				( );

}; // class gui_input
	
}//namespace editor 
}//namespace northstarcpp 
	




#endif // #ifndef GUI_INPUT_H_INCLUDED