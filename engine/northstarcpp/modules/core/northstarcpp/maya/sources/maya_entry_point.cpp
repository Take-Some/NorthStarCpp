////////////////////////////////////////////////////////////////////////////
//	Created		: 07.05.2009
//	Author		: Kayla Verner
// NorthStarCpp project: 2026 Kayla's Systems — Kayla Verner
////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "collision_node.h"
#include "create_collision_cmd.h"
#include "build_collision_cmd.h"
#include "body_node.h"
#include "body_node_manip.h"
#include "joint_node.h"
#include "anim_export_util.h"
#include "skeleton_export_util.h"
#include "create_body_cmd.h"
#include "create_joint_cmd.h"
#include "physics_exporter.h"
#include "solid_visual_exporter.h"
#include "fake_drawing_locator.h"
#include "northstarcpp_shader_node.h"
#include <northstarcpp/core/core.h>
#include <northstarcpp/command_line_extensions.h>
#include <northstarcpp/core/simple_engine.h>
#include "maya_engine.h"
#include <northstarcpp/maya_animation/world.h>
#include <northstarcpp/maya_animation/api.h>
#include <northstarcpp/os_include.h>
#include <northstarcpp/animation/api.h>
#include <northstarcpp/resources_fs.h>
#include <northstarcpp/core/sources/fs_file_system.h>

MCallbackIdArray callbackIds;

northstarcpp::maya::allocator_type			northstarcpp::maya::g_allocator;
northstarcpp::maya::crt_allocator_type		northstarcpp::maya::g_crt_allocator;
northstarcpp::maya::maya_engine				core_engine;
northstarcpp::maya::maya_engine&			northstarcpp::maya::g_maya_engine = core_engine;

void mount_func( bool success)
{
	R_ASSERT(success);
}


NORTHSTARCPP_DLL_EXPORT MStatus initializePlugin( MObject obj )
{
	northstarcpp::core::preinitialize	(
		& core_engine,
		"-fpe_disabled -log_to_stdout",
		northstarcpp::command_line::contains_application_false,
		"maya",
		__DATE__
	);

	northstarcpp::memory::g_crt_allocator	= &northstarcpp::maya::g_crt_allocator;
	
	LOG_INFO( "initialize northstarcpp maya plugin!" );

	northstarcpp::maya::g_allocator.do_register	( 50*1024*1024, "maya" );
	northstarcpp::memory::allocate_region		( );
	northstarcpp::core::initialize				( "maya plugin", northstarcpp::core::no_log, northstarcpp::core::perform_debug_initialization );

	MStatus				stat;
	
	northstarcpp::animation::memory_allocator( northstarcpp::maya::g_allocator );

	core_engine.animation_world = northstarcpp::animation::create_world( core_engine );
	ASSERT( core_engine.animation_world );
	
	northstarcpp::maya_animation::memory_allocator( northstarcpp::maya::g_allocator );
	core_engine.maya_animation_world = northstarcpp::maya_animation::create_world( core_engine );

	ASSERT( core_engine.maya_animation_world );

	MFnPlugin plugin	( obj, "2026 Kayla's Systems \u2014 Kayla Verner", "1.0", "Any");

	build_sphere		(stat);
	if(!stat)			return stat;

	build_cylinder		(stat);
	if(!stat)			return stat;

	stat = plugin.registerTransform( collision_node::typeName,
										collision_node::typeId,
										&collision_node::creator, 
										&collision_node::initialize, 
										MPxTransformationMatrix::creator, 
										MPxTransformationMatrix::baseTransformationMatrixId);

	if (!stat)
		stat.perror("registerNode failed");


	stat = plugin.registerTransform(	body_node::typeName, 
										body_node::typeId, 
										&body_node::creator, 
										&body_node::initialize, 
										MPxTransformationMatrix::creator, 
										MPxTransformationMatrix::baseTransformationMatrixId);
	if (!stat)
		stat.perror("registerNode failed");
	stat = plugin.registerTransform( joint_node::typeName,
										joint_node::typeId,
										&joint_node::creator, 
										&joint_node::initialize, 
										MPxTransformationMatrix::creator, 
										MPxTransformationMatrix::baseTransformationMatrixId);

	if (!stat)
		stat.perror("registerNode failed");

/*
	stat = plugin.registerNode( body_node::typeName,
										body_node::typeId,
										&body_node::creator, 
										&body_node::initialize, 
										MPxNode::kLocatorNode );*/

	stat = plugin.registerNode(			fake_drawing_locator::typeName,
										fake_drawing_locator::typeId,
										&fake_drawing_locator::creator, 
										&fake_drawing_locator::initialize, 
										MPxNode::kLocatorNode );

	if (!stat)
		stat.perror("register body_node::typeName failed");

	stat = plugin.registerNode( body_node_manip::typeName,
										body_node_manip::typeId, 
										&body_node_manip::creator,
										&body_node_manip::initialize, 
										MPxNode::kManipContainer );

	if (!stat)
		stat.perror("registerNode failed");



	stat = plugin.registerFileTranslator("northstarcpp_anim_exporter", NULL, &anim_exporter::creator);
	if (!stat)
		stat.perror("registerFileTranslator failed");

	stat = plugin.registerCommand( "northstarcpp_body", createBodyCmd::creator, createBodyCmd::newSyntax );
	if (!stat)
		stat.perror("registerCommand failed");

	stat = plugin.registerCommand( createCollisionCmd::Name, createCollisionCmd::creator, createCollisionCmd::newSyntax );
	if (!stat)
		stat.perror("registerCommand failed");

	stat = plugin.registerCommand( createJointCmd::Name, createJointCmd::creator, createJointCmd::newSyntax );
	if (!stat)
		stat.perror("registerCommand failed");

	stat = plugin.registerCommand( build_collision_cmd::Name, build_collision_cmd::creator, build_collision_cmd::newSyntax );
	if (!stat)
		stat.perror("registerCommand failed");

	stat = plugin.registerFileTranslator("northstarcpp_ph_exporter", NULL, &physics_exporter::creator);
	if (!stat)
		stat.perror("registerFileTranslator failed");

	stat = plugin.registerFileTranslator("northstarcpp_skeleton_exporter", NULL, &skeleton_exporter::creator);
	if (!stat)
		stat.perror("registerFileTranslator failed");

	stat = plugin.registerCommand(solid_visual_exporter::Name, solid_visual_exporter::creator, solid_visual_exporter::newSyntax );
	if (!stat)
		stat.perror("registerCommand failed");

	const MString	UserClassify( "shader/surface" );

	MString command( "if( `window -exists createRenderNodeWindow` ) {refreshCreateRenderNodeWindow(\"" );

	stat = plugin.registerNode( northstarcpp_shader_node::typeName, 
								northstarcpp_shader_node::typeId, 
								northstarcpp_shader_node::creator, 
								northstarcpp_shader_node::initialize, 
								MPxNode::kDependNode,
								&UserClassify );

	if (!stat)
		stat.perror("registerNode failed");

   command += UserClassify;
   
   command += "\");}\n";

   MGlobal::executeCommand( command );


   northstarcpp::resources::query_mount_disk	("andy", 
								"e:/stk2/resources/library/static", 
								NULL,
								&northstarcpp::maya::g_allocator);

   northstarcpp::fs::set_allocator_thread_id	(northstarcpp::threading::current_thread_id());

	return stat;
}

NORTHSTARCPP_DLL_EXPORT MStatus uninitializePlugin( MObject obj)
{
	MStatus			stat;
	MFnPlugin		plugin( obj );

	stat = plugin.deregisterNode( collision_node::typeId );
	if(!stat) 
		stat.perror("deregisterNode failed");

	stat = plugin.deregisterNode( body_node::typeId );
	if(!stat) 
		stat.perror("deregisterNode failed");

	stat = plugin.deregisterNode( body_node_manip::typeId );
	if(!stat) 
		stat.perror("deregisterNode failed");

	stat = plugin.deregisterNode( fake_drawing_locator::typeId );
	if(!stat) 
		stat.perror("deregisterNode failed");

	stat = plugin.deregisterNode( joint_node::typeId );
	if(!stat) 
		stat.perror("deregisterNode failed");

	stat = plugin.deregisterFileTranslator("northstarcpp_anim_exporter");
	if (!stat)
		stat.perror("registerFileTranslator failed");

	stat = plugin.deregisterCommand( createBodyCmd::Name );
	if (!stat)
		stat.perror("deregisterCommand failed");

	stat = plugin.deregisterCommand( createCollisionCmd::Name );
	if (!stat)
		stat.perror("deregisterCommand failed");

	stat = plugin.deregisterCommand( createJointCmd::Name );
	if (!stat)
		stat.perror("deregisterCommand failed");
	
	stat = plugin.deregisterCommand( build_collision_cmd::Name );
	if (!stat)
		stat.perror("deregisterCommand failed");

	stat = plugin.deregisterFileTranslator("northstarcpp_ph_exporter");
	if (!stat)
		stat.perror("registerFileTranslator failed");


	stat = plugin.deregisterFileTranslator("northstarcpp_skeleton_exporter");
	if (!stat)
		stat.perror("registerFileTranslator failed");
	
	stat = plugin.deregisterCommand( solid_visual_exporter::Name );
	if (!stat)
		stat.perror("deregisterCommand failed");
	
	for (unsigned int i=0; i<callbackIds.length(); i++ ) {
		MMessage::removeCallback( (MCallbackId)callbackIds[i] );
	}
	
	const MString UserClassify( "shader/surface" );

	MString command( "if( `window -exists createRenderNodeWindow` ) {refreshCreateRenderNodeWindow(\"" );

	stat = plugin.deregisterNode( northstarcpp_shader_node::typeId );

	command += UserClassify;
	command += "\");}\n";

	MGlobal::executeCommand( command );

	northstarcpp::maya_animation::destroy_world( core_engine.maya_animation_world );
	northstarcpp::animation::destroy_world( core_engine.animation_world );

	return stat;
}