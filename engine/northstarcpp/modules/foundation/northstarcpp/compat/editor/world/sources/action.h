////////////////////////////////////////////////////////////////////////////
//	Created		: 22.05.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef EDITOR_ACTION_H_INCLUDED
#define EDITOR_ACTION_H_INCLUDED

namespace northstarcpp {
namespace editor {

public enum class checked_state{uncheckable,checked,unchecked};

public interface class action 
{
public:
	virtual void			execute			( ) = 0;
	virtual bool			enabled			( ) = 0;
	virtual	System::String^	name			( );
	virtual	System::String^	text			( ) = 0;
	virtual checked_state	checked_state	( ) = 0;
}; // class action

} // namespace editor
} // namespace northstarcpp


#endif // #ifndef EDITOR_ACTION_H_INCLUDED