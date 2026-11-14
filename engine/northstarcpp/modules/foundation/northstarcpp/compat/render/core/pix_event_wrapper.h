////////////////////////////////////////////////////////////////////////////
//	Created		: 19.02.2010
//	Author		: Armen Abroyan
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PIX_EVENT_WRAPPER_DX11_H_INCLUDED
#define PIX_EVENT_WRAPPER_DX11_H_INCLUDED


#ifdef	DEBUG


#define PIX_EVENT(Name)	pix_event_wrapper_dx11	pixEvent##Name(L#Name)

class pix_event_wrapper_dx11
{
public:
		pix_event_wrapper_dx11(LPCWSTR wszName);
		~pix_event_wrapper_dx11();
};


#else	//	DEBUG

#define PIX_EVENT(Name)	{;}

#endif	//	DEBUG

#endif // #ifndef PIX_EVENT_WRAPPER_DX11_H_INCLUDED
