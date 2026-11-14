////////////////////////////////////////////////////////////////////////////
//	Created		: 29.05.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef MANAGED_DELEGATE_H_INCLUDED
#define MANAGED_DELEGATE_H_INCLUDED

/*public*/ delegate void rqDelegate(northstarcpp::resources::queries_result& data);
/*public*/ delegate void rqDelegate1(northstarcpp::resources::queries_result& data, u32 param);
/*public*/ delegate void grDelegate(northstarcpp::resources::unmanaged_resource_ptr data);
/*public*/ delegate void fsDelegate(northstarcpp::resources::fs_iterator data);

template <class T, class D> struct qr
{
	typedef D Delegate;

	qr(D^ dd):m_delegate(dd){}
	void callback(T data)
	{
		D^ d	= m_delegate;
		d				(data);
		DELETE			(this);
	}
	gcroot<D^>	m_delegate;
};

struct qr1
{
	typedef rqDelegate1 Delegate;

	qr1(rqDelegate1^ dd):m_delegate(dd){}
	void callback(northstarcpp::resources::queries_result& data, u32 param)
	{
		rqDelegate1^ d	= m_delegate;
		d				(data, param);
		DELETE			(this);
	}
	gcroot<rqDelegate1^>	m_delegate;
};

typedef qr<northstarcpp::resources::queries_result&, rqDelegate>			query_result_delegate;
typedef qr1															query_result_delegate1;
typedef qr<northstarcpp::resources::unmanaged_resource_ptr, grDelegate>		resource_delegate;
typedef qr<northstarcpp::resources::fs_iterator, fsDelegate>				fs_iterator_delegate;

#endif // #ifndef MANAGED_DELEGATE_H_INCLUDED