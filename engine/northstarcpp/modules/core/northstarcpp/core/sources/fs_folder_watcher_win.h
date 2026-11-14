/////////////////////////////////////////////////////////////////////////////////
//	Created 	: 01.02.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
/////////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_FS_FOLDER_WATCHER_H_INCLUDED
#define NORTHSTARCPP_FS_FOLDER_WATCHER_H_INCLUDED

#include "fs_watcher.h"
#include <northstarcpp/os_preinclude.h>
#include <northstarcpp/os_include.h>

namespace northstarcpp {
namespace fs {

class folder_watcher : private northstarcpp::detail::noncopyable 
{
public:
	explicit				folder_watcher					(pcstr							folder_name, 
															 file_change_callback const &	callback,
															 threading::event *				receive_changes_event);
							~folder_watcher					();

		void				try_receive_changes				();
		pcstr				path							() const { return m_path.c_str(); }

private:
		enum				swap_buffers_bool				{	swap_buffers_false, swap_buffers_true	};

		void				subscribe_to_receive_changes	(swap_buffers_bool swap);
		void				receive_changes					();

public: // really private
	folder_watcher *		m_next_watcher; // for intrusive_double_linked_list
	folder_watcher *		m_prev_watcher; // for intrusive_double_linked_list

private:
	// set 128 to avoid overflows buffer should be less than 64K to avoid Windows problem with network watches (see MSDN)
	u8						m_buffer[2][ 128*1024 ];
	path_string				m_path;
	threading::event *		m_receive_changes_event;
	pbyte					m_current_buffer;
	pbyte					m_next_buffer;
	OVERLAPPED				m_overlapped;
	file_change_callback	m_callback;
	HANDLE					m_handle;

}; // class folder_watcher

} // namespace fs
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_FS_FOLDER_WATCHER_H_INCLUDED