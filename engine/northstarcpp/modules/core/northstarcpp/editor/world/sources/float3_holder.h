////////////////////////////////////////////////////////////////////////////
//	Created		: 08.06.2010
//	Author		: Plichko Alexander
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef FLOAT3_HOLDER_H_INCLUDED
#define FLOAT3_HOLDER_H_INCLUDED

using namespace System;

namespace northstarcpp {
	namespace editor {

		ref class float3_holder {
		public:
			float3_holder(String^ name, configs::lua_config_value const& config){
				m_name = name;
				m_config = MT_NEW(configs::lua_config_value) (config);
			}

			!float3_holder()
			{
				MT_DELETE	( m_config );
			}

			~float3_holder()
			{
				this->!float3_holder();
			}


		private:
			String^ m_name;
			configs::lua_config_value* m_config;

		public:
			void set(float3 value){
				(*m_config)[(pcstr)unmanaged_string(m_name).c_str()] = value;
			}
			float3 get(){
				return (float3)(*m_config)[(pcstr)unmanaged_string(m_name).c_str()];
			}

		}; // class float_holder



	} // namespace editor
} // namespace northstarcpp

#endif // #ifndef FLOAT3_HOLDER_H_INCLUDED