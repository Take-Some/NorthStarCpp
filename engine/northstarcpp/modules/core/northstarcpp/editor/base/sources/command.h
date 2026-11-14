////////////////////////////////////////////////////////////////////////////
//	Created		: 30.03.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_H_INCLUDED
#define COMMAND_H_INCLUDED

namespace northstarcpp {
namespace editor_base {

//ref class command_engine;

public ref class command abstract
{
//public:
//	friend class northstarcpp::editor::command_engine;
	
public:
	virtual			~command		()	{}
	virtual bool	commit			() = 0;
	virtual void	rollback		() = 0;

	virtual void	start_preview	()	{ };
	virtual bool	end_preview		()	{ return true; };

}; // class command

}// namespace editor
}// namespace northstarcpp

#endif // #ifndef COMMAND_H_INCLUDED