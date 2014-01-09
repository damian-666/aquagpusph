/*
 *  This file is part of AQUAgpusph, a free CFD program based on SPH.
 *  Copyright (C) 2012  Jose Luis Cercos Pita <jl.cercos@upm.es>
 *
 *  AQUAgpusph is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  AQUAgpusph is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with AQUAgpusph.  If not, see <http://www.gnu.org/licenses/>.
 */

// ----------------------------------------------------------------------------
// Include the Screen manager
// ----------------------------------------------------------------------------
#include <ScreenManager.h>

// ----------------------------------------------------------------------------
// Include the main header
// ----------------------------------------------------------------------------
#include <CalcServer/Movements/Movement.h>

// ----------------------------------------------------------------------------
// Include the calculation server
// ----------------------------------------------------------------------------
#include <CalcServer.h>

#ifdef xmlAttribute
	#undef xmlAttribute
#endif
#define xmlAttribute(elem, att) XMLString::transcode( elem->getAttribute(XMLString::transcode(att)) )
using namespace xercesc;

namespace Aqua{ namespace CalcServer{ namespace Movement{

Movement::Movement()
	: Kernel("Movement")
	, _program(0)
	, _kernel(0)
	, _global_work_size(0)
	, _local_work_size(0)
	, _path(0)
{
}

Movement::~Movement()
{
	if(_kernel)clReleaseKernel(_kernel); _kernel=0;
	if(_program)clReleaseProgram(_program); _program=0;
	if(_path) delete[] _path; _path=0;
}

bool Movement::parse(const char* def)
{
	InputOutput::ScreenManager *S = InputOutput::ScreenManager::singleton();
	char msg[1024];
	//! Open the definition file
	if(!strlen(def)) {
	    S->addMessage(3, "(Movement::parse): _path of movement definition file is empty.\n");
	    return true;
	}
	sprintf(msg, "(Movement::parse): Parsing XML file \"%s\"\n", def);
	S->addMessage(1, msg);
	// Try to open as ascii file in order to know if file exist
	FILE *dummy=0; dummy = fopen(def, "r");
	if(!dummy){
	    S->addMessage(3, "(Movement::parse): Can't open file.\n");
	    return true;
	}
	fclose(dummy);
	XercesDOMParser *mparser = new XercesDOMParser;
	mparser->setValidationScheme( XercesDOMParser::Val_Never );
	mparser->setDoNamespaces( false );
	mparser->setDoSchema( false );
	mparser->setLoadExternalDTD( false );
	mparser->parse( def );
	//! Handle document
	DOMDocument* doc = mparser->getDocument();
	DOMElement* root = doc->getDocumentElement();
	if( !root ){
	    S->addMessage(3, "(Movement::parse): Empty XML file\n");
	    return true;
	}
	//! Look for included files to parse it previously
	DOMNodeList* nodes = root->getElementsByTagName(XMLString::transcode("Include"));
	for( XMLSize_t i=0; i<nodes->getLength();i++ ){
	    DOMNode* node = nodes->item(i);
	    if( node->getNodeType() != DOMNode::ELEMENT_NODE )
	        continue;
	    DOMElement* elem = dynamic_cast< xercesc::DOMElement* >( node );
		// Calls recursively to parse xml file
		if(parse( XMLString::transcode( elem->getAttribute(XMLString::transcode("file")) ) ))
			return true;
	}
	//! Look for an OpenCL script file
	nodes = root->getElementsByTagName(XMLString::transcode("Script"));
	for( XMLSize_t i=0; i<nodes->getLength();i++ ){
	    DOMNode* node = nodes->item(i);
	    if( node->getNodeType() != DOMNode::ELEMENT_NODE )
	        continue;
	    DOMElement* elem = dynamic_cast< xercesc::DOMElement* >( node );
	    const char* path = XMLString::transcode( elem->getAttribute(XMLString::transcode("file")) );
	    unsigned int str_len = strlen(path);
	    if(!str_len){
	        S->addMessage(3, "(Movement::parse): Movement OpenCL script is empty.\n");
	        S->addMessage(0, "\tProgram will continue looking for a script.\n");
	        continue;
	    }
		// Change script file
		if(_path) delete[] _path; _path=0;
		_path = new char[str_len+1];
		if(!_path){
	        S->addMessage(3, "(Movement::parse): Can't allocate memory for OpenCL script path.\n");
	        return true;
		}
		strcpy(_path,path);
	}
	//! Parse movement type specific options
	if(_parse(root))
	    return true;
	//! Calls to setupOpenCL
	if(setupOpenCL())
	    return true;
	S->addMessage(1, "(Movement::parse): Ready to work!\n");
	return false;
}

bool Movement::setupOpenCL()
{
	InputOutput::ScreenManager *S = InputOutput::ScreenManager::singleton();
	CalcServer *C = CalcServer::singleton();
	cl_int err_code;
	if(!loadKernelFromFile(&_kernel, &_program, C->context, C->device, _path, "Movement", ""))
	    return true;
	//! Look for work group size
	_local_work_size  = localWorkSize();
	if(!_local_work_size){
	    S->addMessage(3, "(Movement::setupOpenCL): I cannot get a valid local work size for the required computation tool.\n");
	    exit(EXIT_FAILURE);
	}
	cl_device_id device;
	size_t local_work_size=0;
	err_code |= clGetCommandQueueInfo(C->command_queue,CL_QUEUE_DEVICE,
	                                sizeof(cl_device_id),&device, NULL);
	if(err_code != CL_SUCCESS) {
		S->addMessage(3, "(Movement::setupOpenCL): I Cannot get the device from the command queue.\n");
	    return true;
	}
	err_code |= clGetKernelWorkGroupInfo(_kernel,device,CL_KERNEL_WORK_GROUP_SIZE,
	                                   sizeof(size_t), &local_work_size, NULL);
	if(err_code != CL_SUCCESS) {
		S->addMessage(3, "(Movement::setupOpenCL): Failure retrieving the maximum local work size.\n");
	    return true;
	}
	if(local_work_size < _local_work_size)
	    _local_work_size  = local_work_size;
	_global_work_size = globalWorkSize(_local_work_size);
	return false;
}

}}} // namespaces