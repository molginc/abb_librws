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


    void PanelService::setControllerState(ControllerState state)
    {
        std::string uri = Resources::RW_PANEL_CTRLSTATE + "?" + Queries::ACTION_SETCTRLSTATE;
        std::stringstream content;
        content << "ctrl-state=" << state;

        client_.httpPost(uri, content.str());
    }


    unsigned PanelService::getSpeedRatio()
    {
        std::string uri = "/rw/panel/speedratio";
        RWSResult rws_result = parser_.parseString(client_.httpGet(uri).content());

        return std::stoul(xmlFindTextContent(rws_result, XMLAttribute(Identifiers::CLASS, "speedratio")));
    }


    void PanelService::setSpeedRatio(unsigned int ratio)
    {
        if (ratio > 100)
            BOOST_THROW_EXCEPTION(std::out_of_range {"Speed ratio argument out of range (should be 0 <= ratio <= 100)"});

        std::string uri = "/rw/panel/speedratio?action=setspeedratio";
        std::stringstream content;
        content << "speed-ratio=" << ratio;

        client_.httpPost(uri, content.str());
    }
}