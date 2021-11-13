#include <abb_librws/v1_0/rw/panel.h>

#include <abb_librws/parsing.h>
#include <abb_librws/system_constants.h>


namespace abb :: rws :: v1_0 :: rw
{
    PanelService::PanelService(RWSClient& client)
    :   client_ {client}
    {
    }


    ControllerState PanelService::getControllerState()
    {
        std::string uri = Resources::RW_PANEL_CTRLSTATE;
        RWSResult xml_content = parser_.parseString(client_.httpGet(uri).content());

        Poco::XML::Node const * li_node = xml_content->getNodeByPath("html/body/div/ul/li");
        if (!li_node)
            BOOST_THROW_EXCEPTION(ProtocolError {"Cannot parse RWS response: can't find XML path html/body/div/ul/li"});

        std::string const ctrlstate = xmlFindTextContent(li_node, XMLAttributes::CLASS_CTRLSTATE);
        if (ctrlstate.empty())
            BOOST_THROW_EXCEPTION(ProtocolError {"Can't find a node with class=\"ctrlstate\""});

        return makeControllerState(ctrlstate);
    }


    OperationMode PanelService::getOperationMode()
    {
        std::string uri = Resources::RW_PANEL_OPMODE;
        RWSResult xml_content = parser_.parseString(client_.httpGet(uri).content());

        Poco::XML::Node const * li_node = xml_content->getNodeByPath("html/body/div/ul/li");
        if (!li_node)
            BOOST_THROW_EXCEPTION(ProtocolError {"Cannot parse RWS response: can't find XML path html/body/div/ul/li"});

        std::string const opmode = xmlFindTextContent(li_node, XMLAttributes::CLASS_OPMODE);
        if (opmode.empty())
            BOOST_THROW_EXCEPTION(ProtocolError {"Can't find a node with class=\"opmode\""});

        return makeOperationMode(opmode);
    }
}