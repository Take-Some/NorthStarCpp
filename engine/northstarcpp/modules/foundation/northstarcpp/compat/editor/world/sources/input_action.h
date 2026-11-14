////////////////////////////////////////////////////////////////////////////
//	Created		: 12.02.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef INPUT_ACTION_H_INCLUDED
#define INPUT_ACTION_H_INCLUDED

namespace northstarcpp {
namespace editor {

public ref class input_action  abstract 
{
public:
		virtual bool		capture		( ) = 0;
		virtual bool		enabled		( ) = 0;
		virtual void		execute		( ) {};
		virtual void		release		( ) {};
		virtual System::String^	name	( ) = 0;

}; // class input_action

} // namespace editor
} // namespace northstarcpp

#endif // #ifndef INPUT_ACTION_H_INCLUDED