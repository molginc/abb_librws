#pragma once

#include <abb_librws/rws.h>

#include <string>


namespace abb :: rws :: rw
{
    /**
     * \brief Robot controller state.
     */
    enum class ControllerState
    {
        init,
        motorOn,
        motorOff,
        guardStop,
        emergencyStop,
        emergencyStopReset,
        sysFail
    };


    std::ostream& operator<<(std::ostream& os, ControllerState state);


    /**
     * \brief Create \a ControllerState from string.
     *
     * \param str source string
     *
     * \return \a ControllerState matching the value of \a str
     *
     * \throw \a std::invalid_argument if \a str is not from the set of valid strings.
     */
    ControllerState makeControllerState(std::string const& str);
}