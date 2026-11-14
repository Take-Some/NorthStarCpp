////////////////////////////////////////////////////////////////////////////
//	Created		: 20.01.2010
//	Author		: 
//	Copyright (C) Kayla's Systems - 2010
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#ifndef PARTICLE_NODE_CONNECTION_EDITOR_H_INCLUDED
#define PARTICLE_NODE_CONNECTION_EDITOR_H_INCLUDED

namespace northstarcpp {
	namespace editor {

		ref class particle_graph_document;

		public ref class particle_node_connection_editor : public controls::hypergraph::connection_editor
		{
		public:
			particle_node_connection_editor(particle_graph_document^ d):m_document(d)	{};
			virtual void	execute(controls::hypergraph::node^ src, controls::hypergraph::node^ dst);

		private:
			particle_graph_document^ m_document;
		}; // ref class particle_node_connection_editor
	} // namespace editor
} // namespace northstarcpp


#endif // #ifndef PARTICLE_NODE_CONNECTION_EDITOR_H_INCLUDED