#include <abb_librws/common/rw/panel.h>

#include <boost/throw_exception.hpp>

#include <stdexcept>


namespace abb :: rws :: rw
{
    ControllerState makeControllerState(std::string const& str)
    {
        if (str == "init")
            return ControllerState::init;
        else if (str == "motoron")
            return ControllerState::motorOn;
        else if (str == "motorOff")
            return ControllerState::motorOff;
        else if (str == "guardstop")
            return ControllerState::guardStop;
        else if (str == "emergencystop")
            return ControllerState::emergencyStop;
        else if (str == "emergencystopreset")
            return ControllerState::emergencyStopReset;
        else if (str == "sysfail")
            return ControllerState::sysFail;
        else
            BOOST_THROW_EXCEPTION(std::invalid_argument {"Unexpected string representation of controller state: \"" + str + "\""});
    }
}