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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <limits>

#include <InputOutput/State.h>
#include <ScreenManager.h>
#include <ProblemSetup.h>
#include <FileManager.h>
#include <TimeManager.h>
#include <CalcServer.h>
#include <AuxiliarMethods.h>

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
using namespace std;

namespace Aqua{ namespace InputOutput{

State::State()
    : _output_file(NULL)
{
    unsigned int i;
    struct lconv *lc;
    char *s, msg[256];
    ScreenManager *S = ScreenManager::singleton();

    // Set the decimal-point character (which is depending on the locale)
    s = setlocale(LC_NUMERIC, NULL);
    if(strcmp(s, "C")){
        sprintf(msg, "\"%s\" numeric locale found\n", s);
        S->addMessageF(1, msg);
        S->addMessage(0, "\tIt is replaced by \"C\"\n");
        setlocale(LC_NUMERIC, "C");
    }
    lc = localeconv();
    s = lc->decimal_point;
    if(strcmp(s, ".")){
        sprintf(msg, "\"%s\" decimal point character found\n", s);
        S->addMessageF(2, msg);
        S->addMessage(0, "\tIt is replaced by \".\"\n");
        lc->decimal_point = ".";
    }
    s = lc->thousands_sep;
    if(strcmp(s, "")){
        sprintf(msg, "\"%s\" thousands separator character found\n", s);
        S->addMessageF(2, msg);
        S->addMessage(0, "\tIt is removed\n");
        lc->thousands_sep = "";
    }

    // Start the XML parser
    try {
        XMLPlatformUtils::Initialize();
    }
    catch( XMLException& e ){
        char* message = xmlS(e.getMessage());
        S->addMessageF(3, "XML toolkit initialization error.\n");
        char msg[strlen(message) + 3];
        sprintf(msg, "\t%s\n", message);
        S->addMessage(0, msg);
        xmlClear();
        exit(EXIT_FAILURE);
    }
    catch( ... ){
        S->addMessageF(3, "XML toolkit initialization error.\n");
        S->addMessage(0, "\tUnhandled exception\n");
        xmlClear();
        exit(EXIT_FAILURE);
    }

    // Look ofr the first available file place
    i = 0;
    _output_file = new char[256];
    while(true){
        sprintf(_output_file, "AQUAgpusph.save.%u.xml", i);
        FILE *f = fopen(_output_file, "r");
        if(f){
            // The file already exist, look for another one
            i++;
            fclose(f);
            continue;
        }
        break;
    }
}

State::~State()
{
    unsigned int i;
    ScreenManager *S = ScreenManager::singleton();
    // Terminate Xerces
    try{
        XMLPlatformUtils::Terminate();
    }
    catch( xercesc::XMLException& e ){
        char* message = xmlS( e.getMessage() );
        S->addMessageF(3, "XML toolkit exit error.\n");
        char msg[strlen(message) + 3];
        sprintf(msg, "\t%s\n", message);
        S->addMessage(0, msg);
        xmlClear();
        exit(EXIT_FAILURE);
    }
    catch( ... ){
        S->addMessageF(3, "XML toolkit exit error.\n");
        S->addMessage(0, "\tUnhandled exception\n");
        xmlClear();
        exit(EXIT_FAILURE);
    }

    delete[] _output_file;
}

bool State::save()
{
    return write(_output_file);
}

bool State::load()
{
    FileManager *F = FileManager::singleton();
    return parse(F->inputFile());
}

bool State::parse(const char* filepath)
{
    ScreenManager *S = ScreenManager::singleton();
    ProblemSetup *P = ProblemSetup::singleton();
    char msg[1024];
    strcpy(msg, "");

    sprintf(msg, "Parsing the XML file \"%s\"\n", filepath);
    S->addMessageF(1, msg);
    // Try to open as ascii file, just to know if the file already exist
    FILE *dummy=0; dummy = fopen(filepath, "r");
    if(!dummy){
        S->addMessageF(3, "File inaccessible!\n");
        return true;
    }
    fclose(dummy);
    XercesDOMParser *parser = new XercesDOMParser();
    parser->setValidationScheme(XercesDOMParser::Val_Never);
    parser->setDoNamespaces(false);
    parser->setDoSchema(false);
    parser->setLoadExternalDTD(false);
    parser->parse(filepath);
     DOMDocument* doc = parser->getDocument();
    DOMElement* root = doc->getDocumentElement();
    if( !root ){
        S->addMessageF(3, "Empty XML file\n");
        return true;
    }
    // Look for XML files included to parse them before
    DOMNodeList* nodes = root->getElementsByTagName(xmlS("Include"));
    for(XMLSize_t i=0; i<nodes->getLength(); i++){
        DOMNode* node = nodes->item(i);
        if(node->getNodeType() != DOMNode::ELEMENT_NODE)
            continue;
        DOMElement* elem = dynamic_cast<xercesc::DOMElement*>(node);
        const char* included_file = xmlS(elem->getAttribute(xmlS("file")));
        if(parse(included_file)){
            xmlClear();
            return true;
        }
    }

    if(parseSettings(root)){
        xmlClear();
        return true;
    }
    if(parseVariables(root)){
        xmlClear();
        return true;
    }
    if(parseTools(root)){
        xmlClear();
        return true;
    }
    if(parseReports(root)){
        xmlClear();
        return true;
    }
    if(parseTiming(root)){
        xmlClear();
        return true;
    }
    if(parseSet(root)){
        xmlClear();
        return true;
    }

    xmlClear();
    delete parser;
    return false;
}

bool State::parseSettings(DOMElement *root)
{
    ScreenManager *S = ScreenManager::singleton();
    ProblemSetup *P = ProblemSetup::singleton();
    char msg[1024]; strcpy(msg, "");
    DOMNodeList* nodes = root->getElementsByTagName(xmlS("Settings"));
    for(XMLSize_t i=0; i<nodes->getLength();i++){
        DOMNode* node = nodes->item(i);
        if(node->getNodeType() != DOMNode::ELEMENT_NODE)
            continue;
        DOMElement* elem = dynamic_cast<xercesc::DOMElement*>(node);

        DOMNodeList* s_nodes = elem->getElementsByTagName(xmlS("Verbose"));
        for(XMLSize_t j=0; j<s_nodes->getLength(); j++){
            DOMNode* s_node = s_nodes->item(j);
            if(s_node->getNodeType() != DOMNode::ELEMENT_NODE)
                continue;
            DOMElement* s_elem = dynamic_cast<xercesc::DOMElement*>(s_node);
            P->settings.verbose_level = atoi(xmlAttribute(s_elem, "level"));
        }

        s_nodes = elem->getElementsByTagName(xmlS("Device"));
        for(XMLSize_t j=0; j<s_nodes->getLength(); j++){
            DOMNode* s_node = s_nodes->item(j);
            if(s_node->getNodeType() != DOMNode::ELEMENT_NODE)
                continue;
            DOMElement* s_elem = dynamic_cast<xercesc::DOMElement*>(s_node);
            P->settings.platform_id = atoi(xmlAttribute(s_elem, "platform"));
            P->settings.device_id   = atoi(xmlAttribute(s_elem, "device"));
            if(!strcmp("ALL", xmlAttribute(s_elem, "type")))
                P->settings.device_type = CL_DEVICE_TYPE_ALL;
            else if(!strcmp("CPU", xmlAttribute(s_elem, "type")))
                P->settings.device_type = CL_DEVICE_TYPE_CPU;
            else if(!strcmp("GPU", xmlAttribute(s_elem, "type")))
                P->settings.device_type = CL_DEVICE_TYPE_GPU;
            else if(!strcmp("ACCELERATOR", xmlAttribute(s_elem, "type")))
                P->settings.device_type = CL_DEVICE_TYPE_ACCELERATOR;
            else if(!strcmp("DEFAULT", xmlAttribute(s_elem, "type")))
                P->settings.device_type = CL_DEVICE_TYPE_DEFAULT;
            else{
                sprintf(msg,
                        "Unknow \"%s\" type of device\n",
                        xmlAttribute(s_elem, "type"));
                S->addMessageF(3, msg);
                S->addMessage(0, "\tThe valid options are:\n");
                S->addMessage(0, "\t\tALL\n");
                S->addMessage(0, "\t\tCPU\n");
                S->addMessage(0, "\t\tGPU\n");
                S->addMessage(0, "\t\tACCELERATOR\n");
                S->addMessage(0, "\t\tDEFAULT\n");
                return true;
            }
        }
    }
    return false;
}

bool State::parseVariables(DOMElement *root)
{
    ProblemSetup *P = ProblemSetup::singleton();
    DOMNodeList* nodes = root->getElementsByTagName(xmlS("Variables"));
    for(XMLSize_t i=0; i<nodes->getLength(); i++){
        DOMNode* node = nodes->item(i);
        if(node->getNodeType() != DOMNode::ELEMENT_NODE)
            continue;
        DOMElement* elem = dynamic_cast<xercesc::DOMElement*>(node);
        DOMNodeList* s_nodes = elem->getElementsByTagName(xmlS("Variable"));
        for(XMLSize_t j=0; j<s_nodes->getLength(); j++){
            DOMNode* s_node = s_nodes->item(j);
            if(s_node->getNodeType() != DOMNode::ELEMENT_NODE)
                continue;
            DOMElement* s_elem = dynamic_cast<xercesc::DOMElement*>(s_node);
            if(!strstr(xmlAttribute(s_elem, "type"), "*")){
                P->variables.registerVariable(xmlAttribute(s_elem, "name"),
                                              xmlAttribute(s_elem, "type"),
                                              "1",
                                              xmlAttribute(s_elem, "value"));
            }
            else{
                P->variables.registerVariable(xmlAttribute(s_elem, "name"),
                                              xmlAttribute(s_elem, "type"),
                                              xmlAttribute(s_elem, "length"),
                                              "NULL");
            }
        }
    }
    return false;
}

bool State::parseTools(DOMElement *root)
{
    ScreenManager *S = ScreenManager::singleton();
    ProblemSetup *P = ProblemSetup::singleton();
    char msg[1024]; strcpy(msg, "");
    DOMNodeList* nodes = root->getElementsByTagName(xmlS("Tools"));
    for(XMLSize_t i=0; i<nodes->getLength(); i++){
        DOMNode* node = nodes->item(i);
        if(node->getNodeType() != DOMNode::ELEMENT_NODE)
            continue;
        DOMElement* elem = dynamic_cast<xercesc::DOMElement*>(node);
        DOMNodeList* s_nodes = elem->getElementsByTagName(xmlS("Tool"));
        for(XMLSize_t j=0; j<s_nodes->getLength(); j++){
            DOMNode* s_node = s_nodes->item(j);
            if(s_node->getNodeType() != DOMNode::ELEMENT_NODE)
                continue;
            DOMElement* s_elem = dynamic_cast<xercesc::DOMElement*>(s_node);
            if(!xmlHasAttribute(s_elem, "name")){
                S->addMessageF(3, "Found a tool without name\n");
                return true;
            }
            if(!xmlHasAttribute(s_elem, "type")){
                S->addMessageF(3, "Found a tool without type\n");
                return true;
            }

            // Create the tool
            ProblemSetup::sphTool *tool = new ProblemSetup::sphTool();
            tool->set("name", xmlAttribute(s_elem, "name"));
            tool->set("type", xmlAttribute(s_elem, "type"));

            // Place the tool
            if(!xmlHasAttribute(s_elem, "action") ||
               !strcmp(xmlAttribute(s_elem, "action"), "add")){
                P->tools.push_back(tool);
            }
            else if(!strcmp(xmlAttribute(s_elem, "action"), "insert")){
                unsigned int place = 0;
                if(xmlHasAttribute(s_elem, "at")){
                    place = atoi(xmlAttribute(s_elem, "at"));
                }
                else if(xmlHasAttribute(s_elem, "before")){
                    const char *att_str = xmlAttribute(s_elem, "before");
                    for(place = 0; place < P->tools.size(); place++){
                        if(!strcmp(P->tools.at(place)->get("name"),
                                   att_str))
                        {
                            break;
                        }
                    }
                    if(place == P->tools.size()){
                        sprintf(msg,
                                "The tool \"%s\" must be inserted before \"%s\", but such tool cannot be found.\n",
                                tool->get("name"),
                                att_str);
                        S->addMessageF(3, msg);
                        return true;
                    }
                }
                else if(xmlHasAttribute(s_elem, "after")){
                    const char *att_str = xmlAttribute(s_elem, "after");
                    for(place = 0; place < P->tools.size(); place++){
                        if(!strcmp(P->tools.at(place)->get("name"),
                                   att_str))
                        {
                            break;
                        }
                    }
                    if(place == P->tools.size()){
                        sprintf(msg,
                                "The tool \"%s\" must be inserted after \"%s\", but such tool cannot be found.\n",
                                tool->get("name"),
                                att_str);
                        S->addMessageF(3, msg);
                        return true;
                    }
                    place++;
                }
                else{
                    sprintf(msg,
                            "Missed the place where the tool \"%s\" should be inserted.\n",
                            tool->get("name"));
                    S->addMessageF(0, "Please set one of the following attributes:\n");
                    S->addMessageF(0, "\t\"in\"\n");
                    S->addMessageF(0, "\t\"before\"\n");
                    S->addMessageF(0, "\t\"after\"\n");
                    S->addMessageF(3, msg);
                    return true;
                }
                if(place > P->tools.size()){
                    sprintf(msg,
                            "Failure inserting the tool \"%s\" in the place %u (max=%lu).\n",
                            tool->get("name"),
                            place,
                            P->tools.size());
                    S->addMessageF(3, msg);
                    return true;
                }
                P->tools.insert(P->tools.begin() + place, tool);
            }
            else if(!strcmp(xmlAttribute(s_elem, "action"), "remove")){
                unsigned int place;
                for(place = 0; place < P->tools.size(); place++){
                    if(!strcmp(P->tools.at(place)->get("name"),
                               tool->get("name"))){
                        break;
                    }
                }
                if(place == P->tools.size()){
                    sprintf(msg,
                            "Failure removing the tool \"%s\" (tool not found).\n",
                            tool->get("name"));
                    S->addMessageF(3, msg);
                    return true;
                }
                P->tools.erase(P->tools.begin() + place);
                continue;
            }
            else{
                sprintf(msg,
                        "Unknown \"action\" for the tool \"%s\".\n",
                        tool->get("name"));
                S->addMessageF(3, msg);
                S->addMessage(0, "\tThe valid actions are:\n");
                S->addMessage(0, "\t\tadd\n");
                S->addMessage(0, "\t\tinsert\n");
                S->addMessage(0, "\t\tremove\n");
                return true;
            }

            // Configure the tool
            if(!strcmp(xmlAttribute(s_elem, "type"), "kernel")){
                if(!xmlHasAttribute(s_elem, "path")){
                    sprintf(msg,
                            "Tool \"%s\" is of type \"kernel\", but \"path\" is not defined.\n",
                            tool->get("name"));
                    S->addMessageF(3, msg);
                    return true;
                }
                tool->set("path", xmlAttribute(s_elem, "path"));
                if(!xmlHasAttribute(s_elem, "n")){
                    tool->set("n", "N");
                }
                else{
                    tool->set("path", xmlAttribute(s_elem, "n"));
                }
            }
            else if(!strcmp(xmlAttribute(s_elem, "type"), "copy")){
                const char *atts[2] = {"in", "out"};
                for(unsigned int k = 0; k < 2; k++){
                    if(!xmlHasAttribute(s_elem, atts[k])){
                        sprintf(msg,
                                "Tool \"%s\" is of type \"set\", but \"%s\" is not defined.\n",
                                tool->get("name"),
                                atts[k]);
                        S->addMessageF(3, msg);
                        return true;
                    }
                    tool->set(atts[k], xmlAttribute(s_elem, atts[k]));
                }
            }
            else if(!strcmp(xmlAttribute(s_elem, "type"), "python")){
                if(!xmlHasAttribute(s_elem, "path")){
                    sprintf(msg,
                            "Tool \"%s\" is of type \"python\", but \"path\" is not defined.\n",
                            tool->get("name"));
                    S->addMessageF(3, msg);
                    return true;
                }
                tool->set("path", xmlAttribute(s_elem, "path"));
            }
            else if(!strcmp(xmlAttribute(s_elem, "type"), "set")){
                const char *atts[2] = {"in", "value"};
                for(unsigned int k = 0; k < 2; k++){
                    if(!xmlHasAttribute(s_elem, atts[k])){
                        sprintf(msg,
                                "Tool \"%s\" is of type \"set\", but \"%s\" is not defined.\n",
                                tool->get("name"),
                                atts[k]);
                        S->addMessageF(3, msg);
                        return true;
                    }
                    tool->set(atts[k], xmlAttribute(s_elem, atts[k]));
                }
            }
            else if(!strcmp(xmlAttribute(s_elem, "type"), "set_scalar")){
                const char *atts[2] = {"in", "value"};
                for(unsigned int k = 0; k < 2; k++){
                    if(!xmlHasAttribute(s_elem, atts[k])){
                        sprintf(msg,
                                "Tool \"%s\" is of type \"set\", but \"%s\" is not defined.\n",
                                tool->get("name"),
                                atts[k]);
                        S->addMessageF(3, msg);
                        return true;
                    }
                    tool->set(atts[k], xmlAttribute(s_elem, atts[k]));
                }
            }
            else if(!strcmp(xmlAttribute(s_elem, "type"), "reduction")){
                const char *atts[3] = {"in", "out", "null"};
                for(unsigned int k = 0; k < 3; k++){
                    if(!xmlHasAttribute(s_elem, atts[k])){
                        sprintf(msg,
                                "Tool \"%s\" is of type \"reduction\", but \"%s\" is not defined.\n",
                                tool->get("name"),
                                atts[k]);
                        S->addMessageF(3, msg);
                        return true;
                    }
                    tool->set(atts[k], xmlAttribute(s_elem, atts[k]));
                }
                if(!strcmp(xmlS(s_elem->getTextContent()), "")){
                    sprintf(msg,
                            "No operation specified for the reduction \"%s\".\n",
                            tool->get("name"));
                    S->addMessageF(3, msg);
                    return true;
                }
                tool->set("operation", xmlS(s_elem->getTextContent()));
            }
            else if(!strcmp(xmlAttribute(s_elem, "type"), "link-list")){
                if(!xmlHasAttribute(s_elem, "in")){
                    tool->set("in", "pos");
                    continue;
                }
                tool->set("in", xmlAttribute(s_elem, "in"));
            }
            else{
                sprintf(msg,
                        "Unknown \"type\" for the tool \"%s\".\n",
                        tool->get("name"));
                S->addMessageF(3, msg);
                S->addMessage(0, "\tThe valid types are:\n");
                S->addMessage(0, "\t\tkernel\n");
                S->addMessage(0, "\t\tcopy\n");
                S->addMessage(0, "\t\tpython\n");
                S->addMessage(0, "\t\tset\n");
                S->addMessage(0, "\t\tset_scalar\n");
                S->addMessage(0, "\t\treduction\n");
                S->addMessage(0, "\t\tlink-list\n");
                return true;
            }
        }
    }
    return false;
}

bool State::parseTiming(DOMElement *root)
{
    ScreenManager *S = ScreenManager::singleton();
    ProblemSetup *P = ProblemSetup::singleton();
    char msg[1024]; strcpy(msg, "");
    DOMNodeList* nodes = root->getElementsByTagName(xmlS("Timing"));
    for(XMLSize_t i=0; i<nodes->getLength(); i++){
        DOMNode* node = nodes->item(i);
        if(node->getNodeType() != DOMNode::ELEMENT_NODE)
            continue;
        DOMElement* elem = dynamic_cast<xercesc::DOMElement*>(node);
        // Get options
        DOMNodeList* s_nodes = elem->getElementsByTagName(xmlS("Option"));
        for(XMLSize_t j=0; j<s_nodes->getLength(); j++){
            DOMNode* s_node = s_nodes->item(j);
            if(s_node->getNodeType() != DOMNode::ELEMENT_NODE)
                continue;
            DOMElement* s_elem = dynamic_cast<xercesc::DOMElement*>(s_node);
            const char *name = xmlAttribute(s_elem, "name");
            if(!strcmp(name, "End") || !strcmp(name, "SimulationStop")){
                const char *type = xmlAttribute(s_elem, "type");
                if(!strcmp(type, "Time") || !strcmp(type, "T")){
                    P->time_opts.sim_end_mode =
                        P->time_opts.sim_end_mode | __TIME_MODE__;
                    P->time_opts.sim_end_time =
                        atof(xmlAttribute(s_elem, "value"));
                }
                else if(!strcmp(type, "Steps") || !strcmp(type, "S")){
                    P->time_opts.sim_end_mode =
                        P->time_opts.sim_end_mode | __ITER_MODE__;
                    P->time_opts.sim_end_step =
                        atoi(xmlAttribute(s_elem, "value"));
                }
                else if(!strcmp(type, "Frames") || !strcmp(type, "F")){
                    P->time_opts.sim_end_mode =
                        P->time_opts.sim_end_mode | __FRAME_MODE__;
                    P->time_opts.sim_end_frame =
                        atoi(xmlAttribute(s_elem, "value"));
                }
                else {
                    sprintf(msg,
                            "Unknow simulation stop criteria \"%s\"\n",
                            type);
                    S->addMessageF(3, msg);
                    S->addMessage(0, "\tThe valid options are:\n");
                    S->addMessage(0, "\t\tTime\n");
                    S->addMessage(0, "\t\tSteps\n");
                    S->addMessage(0, "\t\tFrames\n");
                    return true;
                }
            }
            else if(!strcmp(name, "LogFile")){
                const char *type = xmlAttribute(s_elem, "type");
                if(!strcmp(type, "No")){
                    P->time_opts.log_mode = __NO_OUTPUT_MODE__;
                }
                else if(!strcmp(type, "FPS")){
                    P->time_opts.log_mode =
                        P->time_opts.log_mode | __FPS_MODE__;
                    P->time_opts.log_fps =
                        atof(xmlAttribute(s_elem, "value"));
                }
                else if(!strcmp(type, "IPF")){
                    P->time_opts.log_mode =
                        P->time_opts.log_mode | __IPF_MODE__;
                    P->time_opts.log_ipf =
                        atoi(xmlAttribute(s_elem, "value"));
                }
                else {
                    sprintf(msg,
                            "Unknow log file print criteria \"%s\"\n",
                            xmlAttribute(s_elem, "type"));
                    S->addMessageF(3, msg);
                    S->addMessage(0, "\tThe valid options are:\n");
                    S->addMessage(0, "\t\tNo\n");
                    S->addMessage(0, "\t\tFPS\n");
                    S->addMessage(0, "\t\tIPF\n");
                    return true;
                }
            }
            else if(!strcmp(name, "Output")){
                const char *type = xmlAttribute(s_elem, "type");
                if(!strcmp(type, "No")){
                    P->time_opts.output_mode = __NO_OUTPUT_MODE__;
                }
                else if(!strcmp(type, "FPS")){
                    P->time_opts.output_mode =
                        P->time_opts.output_mode | __FPS_MODE__;
                    P->time_opts.output_fps =
                        atof(xmlAttribute(s_elem, "value"));
                }
                else if(!strcmp(type, "IPF")){
                    P->time_opts.output_mode =
                        P->time_opts.output_mode | __IPF_MODE__;
                    P->time_opts.output_ipf =
                        atoi(xmlAttribute(s_elem, "value"));
                }
                else {
                    sprintf(msg,
                            "Unknow output file print criteria \"%s\"\n",
                            type);
                    S->addMessageF(3, msg);
                    S->addMessage(0, "\tThe valid options are:\n");
                    S->addMessage(0, "\t\tNo\n");
                    S->addMessage(0, "\t\tFPS\n");
                    S->addMessage(0, "\t\tIPF\n");
                    return true;
                }
            }

            else {
                sprintf(msg, "Unknow timing option \"%s\"\n", xmlAttribute(s_elem, "name"));
                S->addMessageF(3, msg);
                return true;
            }
        }
    }
    return false;
}

bool State::parseSet(DOMElement *root)
{
    ScreenManager *S = ScreenManager::singleton();
    ProblemSetup *P = ProblemSetup::singleton();
    char msg[1024]; strcpy(msg, "");
    DOMNodeList* nodes = root->getElementsByTagName(xmlS("ParticlesSet"));
    for(XMLSize_t i=0; i<nodes->getLength(); i++){
        DOMNode* node = nodes->item(i);
        if(node->getNodeType() != DOMNode::ELEMENT_NODE)
            continue;
        DOMElement* elem = dynamic_cast<xercesc::DOMElement*>(node);
        if(!xmlHasAttribute(elem, "n")){
            S->addMessageF(3, "Found a particles set without \"n\" attribute.\n");
            return true;
        }

        ProblemSetup::sphParticlesSet *Set = new ProblemSetup::sphParticlesSet();
        Set->n(atoi(xmlAttribute(elem, "n")));

        DOMNodeList* s_nodes = elem->getElementsByTagName(xmlS("Scalar"));
        for(XMLSize_t j=0; j<s_nodes->getLength(); j++){
            DOMNode* s_node = s_nodes->item(j);
            if(s_node->getNodeType() != DOMNode::ELEMENT_NODE)
                continue;
            DOMElement* s_elem = dynamic_cast<xercesc::DOMElement*>(s_node);

            const char *name = xmlAttribute(s_elem, "name");
            const char *value = xmlAttribute(s_elem, "value");
            Set->addScalar(name, value);
        }

        s_nodes = elem->getElementsByTagName(xmlS("Load"));
        for(XMLSize_t j=0; j<s_nodes->getLength(); j++){
            DOMNode* s_node = s_nodes->item(j);
            if(s_node->getNodeType() != DOMNode::ELEMENT_NODE)
                continue;
            DOMElement* s_elem = dynamic_cast<xercesc::DOMElement*>(s_node);
            const char *path = xmlAttribute(s_elem, "file");
            const char *format = xmlAttribute(s_elem, "format");
            const char *fields = xmlAttribute(s_elem, "fields");
            Set->input(path, format, fields);
        }

        s_nodes = elem->getElementsByTagName(xmlS("Save"));
        for(XMLSize_t j=0; j<s_nodes->getLength(); j++){
            DOMNode* s_node = s_nodes->item(j);
            if(s_node->getNodeType() != DOMNode::ELEMENT_NODE)
                continue;
            DOMElement* s_elem = dynamic_cast<xercesc::DOMElement*>(s_node);
            const char *path = xmlAttribute(s_elem, "file");
            const char *format = xmlAttribute(s_elem, "format");
            const char *fields = xmlAttribute(s_elem, "fields");
            Set->output(path, format, fields);
        }
        P->sets.push_back(Set);
    }
    return false;
}

bool State::parseReports(DOMElement *root)
{
    ScreenManager *S = ScreenManager::singleton();
    ProblemSetup *P = ProblemSetup::singleton();
    char msg[1024]; strcpy(msg, "");
    DOMNodeList* nodes = root->getElementsByTagName(xmlS("Reports"));
    for(XMLSize_t i=0; i<nodes->getLength(); i++){
        DOMNode* node = nodes->item(i);
        if(node->getNodeType() != DOMNode::ELEMENT_NODE)
            continue;
        DOMElement* elem = dynamic_cast<xercesc::DOMElement*>(node);
        DOMNodeList* s_nodes = elem->getElementsByTagName(xmlS("Report"));
        for(XMLSize_t j=0; j<s_nodes->getLength(); j++){
            DOMNode* s_node = s_nodes->item(j);
            if(s_node->getNodeType() != DOMNode::ELEMENT_NODE)
                continue;
            DOMElement* s_elem = dynamic_cast<xercesc::DOMElement*>(s_node);
            if(!xmlHasAttribute(s_elem, "name")){
                S->addMessageF(3, "Found a report without name\n");
                return true;
            }
            if(!xmlHasAttribute(s_elem, "type")){
                S->addMessageF(3, "Found a report without type\n");
                return true;
            }
            if(!xmlHasAttribute(s_elem, "fields")){
                S->addMessageF(3, "Found a report without fields\n");
                return true;
            }

            // Create the report
            ProblemSetup::sphTool *report = new ProblemSetup::sphTool();
            report->set("name", xmlAttribute(s_elem, "name"));
            report->set("type", xmlAttribute(s_elem, "type"));
            report->set("fields", xmlAttribute(s_elem, "fields"));
            P->reports.push_back(report);

            // Configure the report
            if(!strcmp(xmlAttribute(s_elem, "type"), "screen")){
                if(xmlHasAttribute(s_elem, "bold")){
                    report->set("bold", xmlAttribute(s_elem, "bold"));
                }
                else{
                    report->set("bold", "false");
                }
                if(xmlHasAttribute(s_elem, "color")){
                    report->set("color", xmlAttribute(s_elem, "color"));
                }
                else{
                    report->set("color", "white");
                }
            }
            else if(!strcmp(xmlAttribute(s_elem, "type"), "file")){
                if(!xmlHasAttribute(s_elem, "path")){
                    sprintf(msg,
                            "Report \"%s\" is of type \"file\", but the output \"path\" is not defined.\n",
                            report->get("name"));
                    S->addMessageF(3, msg);
                    return true;
                }
                report->set("path", xmlAttribute(s_elem, "path"));
            }
            else if(!strcmp(xmlAttribute(s_elem, "type"), "particles")){
                if(!xmlHasAttribute(s_elem, "path")){
                    sprintf(msg,
                            "Report \"%s\" is of type \"particles\", but the output \"path\" is not defined.\n",
                            report->get("name"));
                    S->addMessageF(3, msg);
                    return true;
                }
                report->set("path", xmlAttribute(s_elem, "path"));
                if(!xmlHasAttribute(s_elem, "set")){
                    sprintf(msg,
                            "Report \"%s\" is of type \"particles\", but the \"set\" is not defined.\n",
                            report->get("name"));
                    S->addMessageF(3, msg);
                    return true;
                }
                report->set("set", xmlAttribute(s_elem, "set"));
            }
            else if(!strcmp(xmlAttribute(s_elem, "type"), "log")){
            }
            else{
                sprintf(msg,
                        "Unknown \"type\" for the report \"%s\".\n",
                        report->get("name"));
                S->addMessageF(3, msg);
                S->addMessage(0, "\tThe valid types are:\n");
                S->addMessage(0, "\t\tscreen\n");
                S->addMessage(0, "\t\tfile\n");
                return true;
            }
        }
    }
    return false;
}

bool State::write(const char* filepath)
{
    DOMImplementation* impl;
    char msg[64 + strlen(filepath)];
    ScreenManager *S = ScreenManager::singleton();
    sprintf(msg, "Writing \"%s\" SPH state file...\n", filepath);
    S->addMessageF(1, msg);

    impl = DOMImplementationRegistry::getDOMImplementation(xmlS("Range"));
    DOMDocument* doc = impl->createDocument(
        NULL,
        xmlS("sphInput"),
        NULL);
    DOMElement* root = doc->getDocumentElement();

    if(writeSettings(doc, root)){
        xmlClear();
        return true;
    }
    if(writeVariables(doc, root)){
        xmlClear();
        return true;
    }
    if(writeTools(doc, root)){
        xmlClear();
        return true;
    }
    if(writeReports(doc, root)){
        xmlClear();
        return true;
    }
    if(writeTiming(doc, root)){
        xmlClear();
        return true;
    }
    if(writeSet(doc, root)){
        xmlClear();
        return true;
    }

    // Save the XML document to a file
    impl = DOMImplementationRegistry::getDOMImplementation(xmlS("LS"));
    DOMLSSerializer *saver = ((DOMImplementationLS*)impl)->createLSSerializer();

    if(saver->getDomConfig()->canSetParameter(XMLUni::fgDOMWRTFormatPrettyPrint, true))
        saver->getDomConfig()->setParameter(XMLUni::fgDOMWRTFormatPrettyPrint, true);
    saver->setNewLine(xmlS("\r\n"));

    XMLFormatTarget *target = new LocalFileFormatTarget(filepath);
    // XMLFormatTarget *target = new StdOutFormatTarget();
    DOMLSOutput *output = ((DOMImplementationLS*)impl)->createLSOutput();
    output->setByteStream(target);
    output->setEncoding(xmlS("UTF-8"));

    try {
        saver->write(doc, output);
    }
    catch( XMLException& e ){
        char* message = xmlS(e.getMessage());
        S->addMessageF(3, "XML toolkit writing error.\n");
        char msg[strlen(message) + 3];
        sprintf(msg, "\t%s\n", message);
        S->addMessage(0, msg);
        xmlClear();
        exit(EXIT_FAILURE);
    }
    catch( DOMException& e ){
        char* message = xmlS(e.getMessage());
        S->addMessageF(3, "XML DOM writing error.\n");
        char msg[strlen(message) + 3];
        sprintf(msg, "\t%s\n", message);
        S->addMessage(0, msg);
        xmlClear();
        exit(EXIT_FAILURE);
    }
    catch( ... ){
        S->addMessageF(3, "Writing error.\n");
        S->addMessage(0, "\tUnhandled exception\n");
        exit(EXIT_FAILURE);
    }

    target->flush();

    delete target;
    saver->release();
    output->release();
    doc->release();
    xmlClear();

    return false;
}

bool State::writeSettings(xercesc::DOMDocument* doc,
                          xercesc::DOMElement *root)
{
    DOMElement *elem, *s_elem;
    char att[1024];
    ProblemSetup *P = ProblemSetup::singleton();

    elem = doc->createElement(xmlS("Settings"));
    root->appendChild(elem);

    s_elem = doc->createElement(xmlS("Verbose"));
    sprintf(att, "%d", P->settings.verbose_level);
    s_elem->setAttribute(xmlS("level"), xmlS(att));
    elem->appendChild(s_elem);
    s_elem = doc->createElement(xmlS("Device"));
    sprintf(att, "%u", P->settings.platform_id);
    s_elem->setAttribute(xmlS("platform"), xmlS(att));
    sprintf(att, "%u", P->settings.device_id);
    s_elem->setAttribute(xmlS("device"), xmlS(att));
    switch(P->settings.device_type){
    case CL_DEVICE_TYPE_ALL:
        strcpy(att, "ALL");
        break;
    case CL_DEVICE_TYPE_CPU:
        strcpy(att, "CPU");
        break;
    case CL_DEVICE_TYPE_GPU:
        strcpy(att, "GPU");
        break;
    case CL_DEVICE_TYPE_ACCELERATOR:
        strcpy(att, "ACCELERATOR");
        break;
    case CL_DEVICE_TYPE_DEFAULT:
        strcpy(att, "DEFAULT");
        break;
    }
    s_elem->setAttribute(xmlS("type"), xmlS(att));
    elem->appendChild(s_elem);
    return false;
}

bool State::writeVariables(xercesc::DOMDocument* doc,
                           xercesc::DOMElement *root)
{
    DOMElement *elem, *s_elem;
    ProblemSetup *P = ProblemSetup::singleton();

    elem = doc->createElement(xmlS("Variables"));
    root->appendChild(elem);

    /*
    for(it=tags.begin(); it!=tags.end(); it++){
        s_elem = doc->createElement(xmlS(it->first));
        s_elem->setAttribute(xmlS("file"), xmlS(it->second));
        elem->appendChild(s_elem);
    }
    */
    return false;
}

bool State::writeTools(xercesc::DOMDocument* doc,
                           xercesc::DOMElement *root)
{
    unsigned int i, j;
    DOMElement *elem;
    ProblemSetup *P = ProblemSetup::singleton();

    elem = doc->createElement(xmlS("Tools"));
    root->appendChild(elem);

    std::deque<ProblemSetup::sphTool*> tools = P->tools;

    for(i = 0; i < tools.size(); i++){
        elem = doc->createElement(xmlS("Tool"));
        root->appendChild(elem);
        ProblemSetup::sphTool* tool = tools.at(i);

        for(j = 0; j < tool->n(); j++){
            const char* name = tool->getName(j);
            const char* value = tool->get(j);
            elem->setAttribute(xmlS(name), xmlS(value));
        }
    }

    return false;
}

bool State::writeReports(xercesc::DOMDocument* doc,
                         xercesc::DOMElement *root)
{
    unsigned int i, j;
    DOMElement *elem;
    ProblemSetup *P = ProblemSetup::singleton();

    elem = doc->createElement(xmlS("Reports"));
    root->appendChild(elem);

    std::deque<ProblemSetup::sphTool*> reports = P->reports;

    for(i = 0; i < reports.size(); i++){
        elem = doc->createElement(xmlS("Tool"));
        root->appendChild(elem);
        ProblemSetup::sphTool* report = reports.at(i);

        for(j = 0; j < report->n(); j++){
            const char* name = report->getName(j);
            const char* value = report->get(j);
            elem->setAttribute(xmlS(name), xmlS(value));
        }
    }

    return false;
}

bool State::writeTiming(xercesc::DOMDocument* doc,
                        xercesc::DOMElement *root)
{
    DOMElement *elem, *s_elem;
    char att[1024];
    ProblemSetup *P = ProblemSetup::singleton();
    TimeManager *T = TimeManager::singleton();

    elem = doc->createElement(xmlS("Timing"));
    root->appendChild(elem);

    s_elem = doc->createElement(xmlS("Option"));
    s_elem->setAttribute(xmlS("name"), xmlS("Start"));
    sprintf(att, "%g", T->time());
    s_elem->setAttribute(xmlS("value"), xmlS(att));
    sprintf(att, "%g", T->dt());
    s_elem->setAttribute(xmlS("dt"), xmlS(att));
    sprintf(att, "%u", T->step());
    s_elem->setAttribute(xmlS("iter"), xmlS(att));
    sprintf(att, "%u", T->frame());
    s_elem->setAttribute(xmlS("frame"), xmlS(att));
    elem->appendChild(s_elem);

    if(P->time_opts.sim_end_mode & __TIME_MODE__){
        s_elem = doc->createElement(xmlS("Option"));
        s_elem->setAttribute(xmlS("name"), xmlS("End"));
        s_elem->setAttribute(xmlS("type"), xmlS("Time"));
        sprintf(att, "%g", P->time_opts.sim_end_time);
        s_elem->setAttribute(xmlS("value"), xmlS(att));
        elem->appendChild(s_elem);
    }
    if(P->time_opts.sim_end_mode & __ITER_MODE__){
        s_elem = doc->createElement(xmlS("Option"));
        s_elem->setAttribute(xmlS("name"), xmlS("End"));
        s_elem->setAttribute(xmlS("type"), xmlS("Steps"));
        sprintf(att, "%d", P->time_opts.sim_end_step);
        s_elem->setAttribute(xmlS("value"), xmlS(att));
        elem->appendChild(s_elem);
    }
    if(P->time_opts.sim_end_mode & __FRAME_MODE__){
        s_elem = doc->createElement(xmlS("Option"));
        s_elem->setAttribute(xmlS("name"), xmlS("End"));
        s_elem->setAttribute(xmlS("type"), xmlS("Frames"));
        sprintf(att, "%d", P->time_opts.sim_end_frame);
        s_elem->setAttribute(xmlS("value"), xmlS(att));
        elem->appendChild(s_elem);
    }

    if(P->time_opts.log_mode & __FPS_MODE__){
        s_elem = doc->createElement(xmlS("Option"));
        s_elem->setAttribute(xmlS("name"), xmlS("LogFile"));
        s_elem->setAttribute(xmlS("type"), xmlS("FPS"));
        sprintf(att, "%g", P->time_opts.log_fps);
        s_elem->setAttribute(xmlS("value"), xmlS(att));
        elem->appendChild(s_elem);
    }
    if(P->time_opts.log_mode & __IPF_MODE__){
        s_elem = doc->createElement(xmlS("Option"));
        s_elem->setAttribute(xmlS("name"), xmlS("LogFile"));
        s_elem->setAttribute(xmlS("type"), xmlS("IPF"));
        sprintf(att, "%d", P->time_opts.log_ipf);
        s_elem->setAttribute(xmlS("value"), xmlS(att));
        elem->appendChild(s_elem);
    }

    if(P->time_opts.output_mode & __FPS_MODE__){
        s_elem = doc->createElement(xmlS("Option"));
        s_elem->setAttribute(xmlS("name"), xmlS("Output"));
        s_elem->setAttribute(xmlS("type"), xmlS("FPS"));
        sprintf(att, "%g", P->time_opts.output_fps);
        s_elem->setAttribute(xmlS("value"), xmlS(att));
        elem->appendChild(s_elem);
    }
    if(P->time_opts.output_mode & __IPF_MODE__){
        s_elem = doc->createElement(xmlS("Option"));
        s_elem->setAttribute(xmlS("name"), xmlS("Output"));
        s_elem->setAttribute(xmlS("type"), xmlS("IPF"));
        sprintf(att, "%d", P->time_opts.output_ipf);
        s_elem->setAttribute(xmlS("value"), xmlS(att));
        elem->appendChild(s_elem);
    }

    return false;
}

bool State::writeSet(xercesc::DOMDocument* doc,
                     xercesc::DOMElement *root)
{
    unsigned int i, j;
    char att[16];
    DOMElement *elem, *s_elem;
    ProblemSetup *P = ProblemSetup::singleton();
    FileManager *F = FileManager::singleton();

    std::deque<ProblemSetup::sphParticlesSet*> sets = P->sets;

    for(i = 0; i < sets.size(); i++){
        elem = doc->createElement(xmlS("ParticlesSet"));
        sprintf(att, "%u", P->sets.at(i)->n());
        elem->setAttribute(xmlS("n"), xmlS(att));
        root->appendChild(elem);

        for(j = 0; j < sets.at(i)->scalarNames().size(); j++){
            const char* name = sets.at(i)->scalarNames().at(j);
            const char* value = sets.at(i)->scalarValues().at(j);
            s_elem = doc->createElement(xmlS("Scalar"));
            s_elem->setAttribute(xmlS("name"), xmlS(name));
            s_elem->setAttribute(xmlS("value"), xmlS(value));
            elem->appendChild(s_elem);
        }

        unsigned int n = 1;
        char *fields = new char[n];
        strcpy(fields, "");
        for(j = 0; j < sets.at(i)->outputFields().size(); j++){
            const char* field = sets.at(i)->outputFields().at(j);
            n += strlen(field) + 1;
            char *backup = fields;
            fields = new char[n];
            strcpy(fields, backup);
            delete[] backup; backup = NULL;
            strcat(fields, field);
            if(j < sets.at(i)->outputFields().size() - 1)
                strcat(fields, ",");
        }
        s_elem = doc->createElement(xmlS("Load"));
        s_elem->setAttribute(xmlS("file"), xmlS(F->file(i)));
        s_elem->setAttribute(xmlS("format"), xmlS(sets.at(i)->outputFormat()));
        s_elem->setAttribute(xmlS("fields"), xmlS(fields));
        elem->appendChild(s_elem);

        s_elem = doc->createElement(xmlS("Save"));
        s_elem->setAttribute(xmlS("file"), xmlS(sets.at(i)->outputPath()));
        s_elem->setAttribute(xmlS("format"), xmlS(sets.at(i)->outputFormat()));
        s_elem->setAttribute(xmlS("fields"), xmlS(fields));
        elem->appendChild(s_elem);
    }

    return false;
}

}}  // namespace
