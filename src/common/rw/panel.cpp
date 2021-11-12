#include <abb_librws/common/rw/panel.h>

#include <boost/throw_exception.hpp>

#include <stdexcept>
#include <iostream>


namespace abb :: rws :: rw
{
    ControllerState makeControllerState(std::string const& str)
    {
        if (str == "init")
            return ControllerState::init;
        else if (str == "motoron")
            return ControllerState::motorOn;
        else if (str == "motoroff")
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


    std::ostream& operator<<(std::ostream& os, ControllerState state)
    {
        switch (state)
        {
        case ControllerState::init:
            os << "init";
            break;
        case ControllerState::motorOn:
            os << "motoron";
            break;
        case ControllerState::motorOff:
            os << "motoroff";
            break;
        case ControllerState::guardStop:
            os << "guardstop";
            break;
        case ControllerState::emergencyStop:
            os << "emergencystop";
            break;
        case ControllerState::emergencyStopReset:
            os << "emergencystopreset";
            break;
        case ControllerState::sysFail:
            os << "sysfail";
            break;
        default:
            BOOST_THROW_EXCEPTION(std::logic_error {"Invalid StopMode value"});
        }

        return os;
    }
}