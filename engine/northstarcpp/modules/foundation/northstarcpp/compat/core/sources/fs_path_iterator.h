////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PATH_ITERATOR_H_INCLUDED
#define PATH_ITERATOR_H_INCLUDED

#include "northstarcpp/compat/fixed_string.h"

namespace northstarcpp {
namespace fs {

//-----------------------------------------------------------------------------------
// path_iterator
//-----------------------------------------------------------------------------------

class path_iterator
{
public:
	path_iterator(const char* path_str);
	path_iterator(const char* path_str, int len);

	static 
	path_iterator			end			() { return path_iterator(NULL); }

	void					to_string	(buffer_string& out_str) const;
	bool					equal		(const char* s) const;

	void					operator ++	();
	bool					operator == (const path_iterator& it) const;
	bool					operator != (const path_iterator& it) const;

protected:
	path_iterator() {} // intentionally

	const char* m_path_str;
	const char* m_path_end;
	const char* m_cur_str;
	const char* m_cur_end;
};

//-----------------------------------------------------------------------------------
// reverse_path_iterator
//-----------------------------------------------------------------------------------

class reverse_path_iterator : public path_iterator
{
public:
	reverse_path_iterator				(const char* path_str);
	reverse_path_iterator				(const char* path_str, int len);

	static 
	reverse_path_iterator	end			() { return reverse_path_iterator(NULL); }

	void					operator ++	();
	bool					operator == (const reverse_path_iterator& it) const;
	bool					operator != (const reverse_path_iterator& it) const;
};

} // namespace fs
} // namespace northstarcpp

#include "fs_path_iterator_inline.h"

#endif // PATH_ITERATOR_H_INCLUDED