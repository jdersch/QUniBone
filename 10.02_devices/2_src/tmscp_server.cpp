/*
    tmscp_server.cpp: Implementation of a simple TMSCP server.

    Copyright J. Dersch 2026
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
        mscp_server_base(port)
{
    name.value = "tmscp_server" ;
    type_name.value = "tmscp_server_c";
    log_label = "TMSSVR";
}

tmscp_server::~tmscp_server()
{

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
    
    case Opcodes::ERASE_GAP:

        break;

    case Opcodes::REPOSITION:

        break;

    case Opcodes::WRITE_TAPE_MARK:

        break;

    default:
        // Call base MSCP implementation, then:
        cmdStatus = mscp_server_base::DispatchCommand(message, header, modifiers, protocolError);
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

    // TODO: append the above

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
    tmscp_drive_c* drive = nullptr;
    if (unitNumber < port->GetDriveCount())
    {
        drive = dynamic_cast<tmscp_drive_c*>(port->GetDrive(unitNumber));
    }

    return drive;
}

uint32_t 
tmscp_server::Access(std::shared_ptr<Message> message, uint16_t unitNumber) 
{
    return 0;
}

uint32_t 
tmscp_server::Available(uint16_t unitNumber, uint16_t modifiers)
{
    return 0;
}

uint32_t 
tmscp_server::CompareHostData(std::shared_ptr<Message> message, uint16_t unitNumber)
{
    return 0;
}

uint32_t 
tmscp_server::Erase(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers)
{
    return 0;
}

uint32_t 
tmscp_server::GetUnitStatus(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers)
{
    return 0;
}

uint32_t 
tmscp_server::Online(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers)
{
    return 0;
}

uint32_t 
tmscp_server::Read(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers)
{
    return 0;
}

uint32_t 
tmscp_server::SetControllerCharacteristics(std::shared_ptr<Message> message)
{
    return 0;
}

uint32_t 
tmscp_server::SetUnitCharacteristics(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers)
{
    return 0;
}

uint32_t 
tmscp_server::Write(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers)
{
    return 0;
}

}  // end namespace