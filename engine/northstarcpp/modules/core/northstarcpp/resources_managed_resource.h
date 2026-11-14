////////////////////////////////////////////////////////////////////////////
//	Created		: 06.05.2010
//	Author		: Kayla Verner
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef NORTHSTARCPP_RESOURCES_MANAGED_RESOURCE_H_INCLUDED
#define NORTHSTARCPP_RESOURCES_MANAGED_RESOURCE_H_INCLUDED

#include <northstarcpp/fs_path_string.h>
#include <northstarcpp/resources_classes.h>
#include <northstarcpp/resources_intrusive_base.h>
#include <northstarcpp/resources_resource_base.h>
#include <northstarcpp/resources_resource_ptr.h>

namespace northstarcpp {
namespace resources {

class	managed_node;

template <class T>
class	pinned_ptr_const;

class NORTHSTARCPP_CORE_API managed_resource : public resource_base, public resources::managed_intrusive_base
{
public:
						   ~managed_resource		();

	void					get_full_path			(fs::path_string & dest) const;
	u32						get_size				() const	{ return m_size; }

private:
							managed_resource		(u32 size, class_id	class_id = unknown_data_class);

	pcbyte					pin						();
	void					unpin					(pcbyte pinned_data);

	managed_resource *		get_next_delay_delete	() const { return m_next_delay_delete; }
	void					resize_down				(u32 new_size);
	void					set_is_unmovable		(bool is_unmovable);
	bool					is_unmovable			() const;
	u32						get_buffer_size			() const;
	class_id				get_class_id			() const { return m_class_id; }
	void					late_set_fat_it			(fat_it_wrapper it);
	fat_it_wrapper			get_fat_it				() const { return m_fat_it; }
	fixed_string512			log_string				() const;
	void					set_creation_source		(creation_source_enum creation_source, pcstr request_path);

private:
	fat_it_wrapper			m_fat_it;
	managed_node *			m_node;
	managed_resource *		m_next_delay_delete;
	u32						m_size;
	class_id				m_class_id;
#ifndef MASTER_GOLD
	fs::path_string			m_request_path;
#endif

	friend class			resource_base;
	friend class			query_result;
	friend class			resource_allocator;
	friend class			managed_resource_allocator;
	friend class			resources_manager;
	friend class			thread_local_data;
	friend class			managed_intrusive_base;
	friend class			base_of_intrusive_base;
	friend class			cook_base;
	friend class			game_resources_manager;

	template <class T>
	friend class			pinned_ptr_base;

	friend class			tester_pinned_resource; // tester class
};

typedef	resource_ptr		<managed_resource, managed_intrusive_base>		managed_resource_ptr;
typedef	child_resource_ptr	<managed_resource, managed_intrusive_base>		child_managed_resource_ptr;

namespace resources_detail {
	
	template class NORTHSTARCPP_CORE_API northstarcpp::intrusive_ptr< northstarcpp::resources::managed_resource, 
													  northstarcpp::resources::managed_intrusive_base, 
													  northstarcpp::threading::multi_threading_mutex_policy >;

	template class NORTHSTARCPP_CORE_API northstarcpp::resources::resource_ptr< northstarcpp::resources::managed_resource, 
																northstarcpp::resources::managed_intrusive_base >;

	template class NORTHSTARCPP_CORE_API northstarcpp::resources::child_resource_ptr<	northstarcpp::resources::managed_resource, 
																		northstarcpp::resources::managed_intrusive_base >;

} // namespace resources_detail

} // namespace resources
} // namespace northstarcpp

#endif // #ifndef NORTHSTARCPP_RESOURCES_MANAGED_RESOURCE_H_INCLUDED