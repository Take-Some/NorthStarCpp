////////////////////////////////////////////////////////////////////////////
//	Created		: 30.03.2009
//	Author		: Armen Abroyan
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef COMMAND_ENGINE_H_INCLUDED
#define COMMAND_ENGINE_H_INCLUDED

#include "command.h"

namespace northstarcpp {
namespace editor_base {

public ref class command_engine
{
public:
							command_engine	( u32 stack_limit );
			virtual			~command_engine	( );
			virtual	bool	run				( northstarcpp::editor_base::command^ command );
			virtual	int		rollback		( int steps );

			virtual	bool	run_preview		( northstarcpp::editor_base::command^ command );
			virtual	bool	preview			( );
			virtual	void	end_preview		( );
			
			virtual	void	clear_stack		( );
	inline	virtual	bool	undo_stack_empty( );
	inline	virtual	bool	redo_stack_empty( );
	inline	virtual	bool	stack_empty		( );

			virtual	void	set_saved_flag	( );
			virtual	bool	saved_flag		( );

			void	rollback_transaction( int steps );
private:
	static	void	call_command_destructor	( northstarcpp::editor_base::command^ command );
			void	clear_undo_stack	( );
			void	clear_redo_stack	( );
			void	finalize_old		( );

	inline	void	push_command		( northstarcpp::editor_base::command^ command );
	inline	void	pop_command			( );
			void	insert_transaction_command( );
			bool	run_impl			( northstarcpp::editor_base::command^ command );

private:
	typedef	System::Collections::Generic::List<northstarcpp::editor_base::command^>	command_stack;

private:
	command_stack			m_command_stack;

	int						m_stack_pointer;
	u32						m_stack_limit;

	int						m_count_trans_commands;
	bool					m_reverting_transaction;
	bool					m_start_revert_transaction;
	bool					m_revert_mode;
	bool					m_transaction_started;
	bool					m_revert_transaction_started;
	bool					m_save_flag;
	int						m_saved_stack_pointer;
	int						m_transaction_commands_count;
	northstarcpp::editor_base::command^	m_preview_command;
	bool					m_preview_failed;
	
}; // class command_engine
}// namespace editor_base
}// namespace northstarcpp

#include "command_engine_inline.h"

#endif // #ifndef COMMAND_ENGINE_H_INCLUDED