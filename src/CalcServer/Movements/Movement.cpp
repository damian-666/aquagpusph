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

/** @file
 * @brief Motions base class.
 * (See Aqua::CalcServer::Movement::Movement for details)
 */

#include <ScreenManager.h>
#include <CalcServer/Movements/Movement.h>
#include <CalcServer.h>

#include <vector>
#include <deque>
static std::deque<char*> cpp_str;
static std::deque<XMLCh*> xml_str;

static char *xmlTranscode(const XMLCh *txt)
{
    char *str = xercesc::XMLString::transcode(txt);
    cpp_str.push_back(str);
    return str;
}

static XMLCh *xmlTranscode(const char *txt)
{
    XMLCh *str = xercesc::XMLString::transcode(txt);
    xml_str.push_back(str);
    return str;
}

static void xmlClear()
{
    unsigned int i;
    for(i = 0; i < cpp_str.size(); i++){
        xercesc::XMLString::release(&cpp_str.at(i));
    }
    cpp_str.clear();
    for(i = 0; i < xml_str.size(); i++){
        xercesc::XMLString::release(&xml_str.at(i));
    }
    xml_str.clear();
}

#ifdef xmlS
    #undef xmlS
#endif // xmlS
#define xmlS(txt) xmlTranscode(txt)

#ifdef xmlAttribute
    #undef xmlAttribute
#endif
#define xmlAttribute(elem, att) xmlS( elem->getAttribute(xmlS(att)) )

#ifdef xmlHasAttribute
    #undef xmlHasAttribute
#endif
#define xmlHasAttribute(elem, att) elem->hasAttribute(xmlS(att))

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

    if(!strlen(def)) {
        S->addMessageF(3, "The path of the movement definition file is empty.\n");
        return true;
    }
    sprintf(msg, "Parsing XML file \"%s\"\n", def);
    S->addMessageF(1, msg);
    // Try to open as ascii file in order to know if file exist
    FILE *dummy=0; dummy = fopen(def, "r");
    if(!dummy){
        S->addMessageF(3, "The file cannot be accessed.\n");
        return true;
    }
    fclose(dummy);
    XercesDOMParser *parser = new XercesDOMParser();
    parser->setValidationScheme( XercesDOMParser::Val_Never );
    parser->setDoNamespaces( false );
    parser->setDoSchema( false );
    parser->setLoadExternalDTD( false );
    parser->parse(def);

    DOMDocument* doc = parser->getDocument();
    DOMElement* root = doc->getDocumentElement();
    if( !root ){
        S->addMessageF(3, "Empty XML file\n");
        xmlClear();
        delete parser;
        return true;
    }

    DOMNodeList* nodes = root->getElementsByTagName(xmlS("Include"));
    for( XMLSize_t i=0; i<nodes->getLength();i++ ){
        DOMNode* node = nodes->item(i);
        if( node->getNodeType() != DOMNode::ELEMENT_NODE )
            continue;
        DOMElement* elem = dynamic_cast< xercesc::DOMElement* >( node );
        // Calls recursively to parse xml file
        if(parse(xmlS(elem->getAttribute(xmlS("file"))))){
            xmlClear();
            delete parser;
            return true;
        }
    }

    nodes = root->getElementsByTagName(xmlS("Script"));
    for( XMLSize_t i=0; i<nodes->getLength();i++ ){
        DOMNode* node = nodes->item(i);
        if( node->getNodeType() != DOMNode::ELEMENT_NODE )
            continue;
        DOMElement* elem = dynamic_cast< xercesc::DOMElement* >( node );
        const char* path = xmlS(elem->getAttribute(xmlS("file")));
        unsigned int str_len = strlen(path);
        if(!str_len){
            S->addMessageF(3, "Movement OpenCL script is empty.\n");
            S->addMessage(0, "\tProgram will continue looking for a script.\n");
            continue;
        }
        // Change script file
        if(_path) delete[] _path; _path=0;
        _path = new char[str_len+1];
        if(!_path){
            S->addMessageF(3, "I Cannot allocate memory for OpenCL script path.\n");
            xmlClear();
            delete parser;
            return true;
        }
        strcpy(_path,path);
    }

    if(_parse(root)){
        xmlClear();
        delete parser;
        return true;
    }

    xmlClear();
    delete parser;

    if(setupOpenCL())
        return true;

    S->addMessageF(1, "Ready to work!\n");
    return false;
}

bool Movement::setupOpenCL()
{
    InputOutput::ScreenManager *S = InputOutput::ScreenManager::singleton();
    CalcServer *C = CalcServer::singleton();
    cl_int err_code;
    if(!loadKernelFromFile(&_kernel,
                           &_program,
                           C->context,
                           C->device,
                           _path,
                           "Movement",
                           ""))
        return true;

    _local_work_size  = localWorkSize();
    if(!_local_work_size){
        S->addMessageF(3, "I cannot get a valid local work size for the required computation tool.\n");
        S->printOpenCLError(err_code);
        return true;
    }
    cl_device_id device;
    size_t local_work_size=0;
    err_code = clGetCommandQueueInfo(C->command_queue,
                                     CL_QUEUE_DEVICE,
                                     sizeof(cl_device_id),
                                     &device,
                                     NULL);
    if(err_code != CL_SUCCESS) {
        S->addMessageF(3, "I Cannot get the device from the command queue.\n");
        S->printOpenCLError(err_code);
        return true;
    }
    err_code = clGetKernelWorkGroupInfo(_kernel,
                                        device,
                                        CL_KERNEL_WORK_GROUP_SIZE,
                                        sizeof(size_t),
                                        &local_work_size,
                                        NULL);
    if(err_code != CL_SUCCESS) {
        S->addMessageF(3, "Failure retrieving the maximum local work size.\n");
        S->printOpenCLError(err_code);
        return true;
    }
    if(local_work_size < _local_work_size)
        _local_work_size  = local_work_size;
    _global_work_size = globalWorkSize(_local_work_size);
    return false;
}

}}} // namespaces