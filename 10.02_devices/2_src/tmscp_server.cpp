/*
    tmscp_server.cpp: Implementation of a simple TMSCP server, on top of the base
                      MSCP server (see mscp_server.cpp)

    Copyright 2025 J. Dersch
    Contributed under the BSD 2-clause license.

    
*/
#include <assert.h>
#include <cstddef>
#include <pthread.h>
#include <stdio.h>
#include <memory>
#include <queue>
 
#include "logger.hpp"
#include "utils.hpp"

#include "tmscp_drive.hpp"
#include "tmscp_server.hpp"

namespace mscp
{

tmscp_server::tmscp_server(uda_c* port) :
        mscp_server(port)
{
    
}

tmscp_server::~tmscp_server()
{

}

void tmscp_server::Reset(void) {

}

bool tmscp_server::on_param_changed(parameter_c *param) 
{
    // no own parameter or "enable" logic
    if (param == &enabled) 
    {
        // accept, but do not react on enable/disable, always active
        return true;
    }
    return device_c::on_param_changed(param); // more actions (for enable)
}

void tmscp_server::SetMetadata()
{
    name.value = "tmscp_server" ;
    type_name.value = "tmscp_server_c";
    log_label = "TMSSVR";
}

uint32_t tmscp_server::DispatchCommand(const std::shared_ptr<Message> message, const ControlMessageHeader* header, uint16_t modifiers, bool *protocolError)
{
     /* 21     4.3  Tape Specific MSCP Commands And Responses

        22     Following is a list of Tape specific  MSCP  commands, which  are
        23     described in Chapter 5.

        24           o  ACCESS
        25           o  AVAILABLE
        26           o  COMPARE HOST DATA
        27           o  ERASE
        28           o  ERASE GAP
        29           o  GET UNIT STATUS
        30           o  ONLINE
        31           o  READ
        32           o  REPOSITION
        33           o  SET UNIT CHARACTERISTICS
        34           o  WRITE
        35           o  WRITE TAPE MARK */

    uint32_t cmdStatus = 0;
    switch (header->Word3.Command.Opcode)
    {
    case Opcodes::ACCESS:

        break;

    case Opcodes::AVAILABLE:

        break;

    default:
        // Call base MSCP implementation, then:
        return mscp_server::DispatchCommand(message, header, modifiers, protocolError);
        break;
    }

    // Immediate Command completion for asynchronous returns, etc.

    // 3.1.5
    //  20     The "EOT Encountered" end flag, as described in section 4.5.1, is
    //  21     considered part of the tape unit state.The current state of the
    //  22     "EOT encountered" end flag is returned in the end messages of all
    //  23     commands  which  specify  the  Unit Number field(including those
    //  24     defined only in MSCP).

    // also following paragraphs for other state...

    return cmdStatus;
}


//
// GetDrive():
//  Returns tmscp_drive_c object pointer for the specified unit number,
//  or nullptr if no such object exists.
//
static tmscp_drive_c *
GetDrive(
    uda_c * port,
    uint32_t unitNumber)
{
    // Sanity check for now; if this is being called we had better be attached to an TMSCP port:
    // Would be nice to make this cleaner.
    assert(port->GetPortType() == PortType::TMSCP);

    tmscp_drive_c* drive = nullptr;
    if (unitNumber < port->GetDriveCount())
    {
        drive = dynamic_cast<tmscp_drive_c*>(port->GetDrive(unitNumber));
    }

    return drive;
}

}  // end namespace