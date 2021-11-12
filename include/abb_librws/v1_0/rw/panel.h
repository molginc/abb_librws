#pragma once

#include <abb_librws/v1_0/rws_client.h>

#include <abb_librws/rws.h>
#include <abb_librws/common/rw/panel.h>

#include <Poco/DOM/DOMParser.h>

#include <string>


namespace abb :: rws :: v1_0 :: rw
{
    using namespace rws::rw;


    class PanelService
    {
    public:
        explicit PanelService(RWSClient& client);

        /**
         * \brief A method for retrieving the controller state.
         *
         * \return RWSResult containing the result.
         *
         * \throw \a RWSError if something goes wrong.
         */
        ControllerState getControllerState();

    private:

        RWSClient& client_;
        Poco::XML::DOMParser parser_;
    };
}