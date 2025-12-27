/*
    mscp_server_base.cpp: Base implementation of a simple MSCP server.

    Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
    Copyright J. Dersch 2019-2026
    Contributed under the BSD 2-clause license.

    This provides an implementation of the Minimal MSCP subset outlined
    in AA-L619A-TK (Chapter 6).  It takes a few liberties and errs on 
    the side of implementation simplicity.

    This implements the functionality shared between Disk and Tape (MSCP and TMSCP)
    controllers.  Subclasses implement disk/tape-specific commands and behaviors.
*/
#include <assert.h>
#include <cstddef>
#include <pthread.h>
#include <stdio.h>
#include <memory>
#include <queue>
 
#include "logger.hpp"
#include "utils.hpp"

#include "mscp_drive.hpp"
#include "mscp_server_base.hpp"
#include "uda.hpp"

namespace mscp {

//
// polling_worker():
//  Runs the main MSCP polling thread.
//
void* polling_worker(
    void *context)
{
    mscp_server* server = reinterpret_cast<mscp_server*>(context);
    server->Poll();
    return nullptr;
}

mscp_server_base::mscp_server_base(
    uda_c *port) :
        device_c(),
        _abort_polling(false),
        _pollState(PollingState::Wait),
        polling_cond(PTHREAD_COND_INITIALIZER),
        polling_mutex(PTHREAD_MUTEX_INITIALIZER),
        _credits(INIT_CREDITS) 
{
    set_workers_count(0);
    _port = port;

    enabled.set(true); 
    enabled.readonly = true; // always active

    StartPollingThread();
}

mscp_server_base::~mscp_server_base()
{
    AbortPollingThread();
}


bool mscp_server_base::on_param_changed(parameter_c *param) 
{
    // Basic stub; we have nothing that can be configured.
    if (param == &enabled) 
    {
        return true;
    }
    return device_c::on_param_changed(param);
}

//
// StartPollingThread():
//  Initializes the MSCP polling thread and starts it running.
// 
void
mscp_server_base::StartPollingThread(void)
{
    _abort_polling = false;
    _pollState = PollingState::Wait;

    //
    // Initialize the polling thread and start it.
    // It will wait to be woken to do actual work.
    //
    pthread_attr_t attribs;
    pthread_attr_init(&attribs);

    int status = pthread_create(
        &polling_pthread,
        &attribs,
        &polling_worker,
        reinterpret_cast<void*>(this));

    if (status != 0)
    {
        FATAL("Failed to start mscp server thread.  Status 0x%x", status);
    }

    DEBUG_FAST("Polling thread created.");
}

//
// AbortPollingThread():
//  Stops the MSCP polling thread.
//
void
mscp_server_base::AbortPollingThread(void)
{
    pthread_mutex_lock(&polling_mutex);
    _abort_polling = true;
    _pollState = PollingState::Wait;
    pthread_cond_signal(&polling_cond);
    pthread_mutex_unlock(&polling_mutex);

    pthread_cancel(polling_pthread);

    uint32_t status = pthread_join(polling_pthread, NULL);

    if (status != 0)
    {
        FATAL("Failed to join polling thread, status 0x%x", status);
    }

    DEBUG_FAST("Polling thread aborted.");  
}

//
// Poll():
//  The MSCP polling thread.  
//  This thread waits to be awoken, then pulls messages from the MSCP command
//  ring and executes them.  When no work is left to be done, it goes back to
//  sleep.
//  This is awoken by a write to the UDA IP register.
//
void
mscp_server_base::Poll(void)
{
    worker_init_realtime_priority(rt_device);

    while(!_abort_polling)
    {
        //
        // Wait to be awoken, then pull commands from the command ring
        //
        pthread_mutex_lock(&polling_mutex);
        while (_pollState == PollingState::Wait)
        {
            pthread_cond_wait(
                &polling_cond,
                &polling_mutex);
        }

        // Shouldn't happen but if it does we just return to the top.
        if (_pollState == PollingState::InitRun)
        {
           _pollState = PollingState::Run;
        }

        pthread_mutex_unlock(&polling_mutex);
    
        if (_abort_polling)
        {
            break;
        }

        //
        // Read all commands from the ring into a queue; then execute them.
        //
        std::queue<std::shared_ptr<Message>> messages;

        int msgCount = 0;
        while (!_abort_polling && _pollState != PollingState::InitRestart)
        {
            bool error = false;
            std::shared_ptr<Message> message(_port->GetNextCommand(&error));
            if (error)
            {
                DEBUG_FAST("Error while reading messages, returning to idle state.");
                // The lords of STL decreed that queue should have no "clear" method
                // so we do this garbage instead:
                messages = std::queue<std::shared_ptr<Message>>(); 
                break; 
            }
            if (nullptr == message)
            {
                DEBUG_FAST("End of command ring; %d messages to be executed.", msgCount);
                break;
            }

            msgCount++;
            messages.push(message);
        } 

        //
        // Pull commands from the queue until it is empty or we're told to quit.
        //
        while(!messages.empty() && !_abort_polling && _pollState != PollingState::InitRestart)
        {
            std::shared_ptr<Message> message(messages.front());  
            messages.pop();

            //
            // Handle the message.  We dispatch on opcodes to the
            // appropriate methods.  These methods modify the message
            // object in place; this message object is then posted back
            // to the response ring.
            //
            ControlMessageHeader* header = 
                reinterpret_cast<ControlMessageHeader*>(message->Message);

            DEBUG_FAST("Message size 0x%x opcode 0x%x rsvd 0x%x mod 0x%x unit %d, ursvd 0x%x, ref 0x%x", 
                message->MessageLength,
                header->Word3.Command.Opcode,
                header->Word3.Command.Reserved,
                header->Word3.Command.Modifiers,
                header->UnitNumber,
                header->Reserved,
                header->ReferenceNumber);

            bool protocolError = false;
            uint32_t cmdStatus = 0;
            uint16_t modifiers = header->Word3.Command.Modifiers;

            // Execute the MSCP/TMSCP command
            cmdStatus = DispatchCommand(message, header, modifiers, &protocolError);

            if (protocolError)
            {
                uint16_t subCode = offsetof(ControlMessageHeader, Word3) + HEADER_OFFSET;
                cmdStatus = STATUS(Status::INVALID_COMMAND, subCode, 0);
            }

            DEBUG_FAST("cmd 0x%x st 0x%x fl 0x%x", cmdStatus, GET_STATUS(cmdStatus), GET_FLAGS(cmdStatus));

            //
            // Set the endcode and status bits
            //
            header->Word3.End.Status = GET_STATUS(cmdStatus);
            header->Word3.End.Flags = GET_FLAGS(cmdStatus);

            // Set the End code properly -- for a protocol error, 
            // this is just the End code, for all others it's the End code
            // or'd with the original opcode.
            if (protocolError)
            {
                 // Just the END code, no opcode
                 header->Word3.End.Endcode = Endcodes::END;
            }
            else
            {
                 header->Word3.End.Endcode |= Endcodes::END;
            }

            if (message->Word1.Info.MessageType == MessageTypes::Sequential &&
                header->Word3.End.Endcode & Endcodes::END)
            {
                //
                // We steal the credits hack from simh:
                // The controller gives all of its credits to the host,
                // thereafter it supplies one credit for every response
                // packet sent.
                // 
                uint8_t grantedCredits = std::min(_credits, static_cast<uint8_t>(MAX_CREDITS));
                _credits -= grantedCredits;
                message->Word1.Info.Credits = grantedCredits + 1;
                DEBUG_FAST("granted credits %d", grantedCredits + 1);
            }
            else
            {
                message->Word1.Info.Credits = 0;
            }

            //
            // Post the response to the port's response ring.
            // If everything is working properly, there should always be room.
            //
            if(!_port->PostResponse(message.get()))
            {
                FATAL("Unexpected: no room in response ring.");
            }

            //
            // Go around and pick up the next one.
            //
        }

        //
        // Go back to sleep.  If a UDA reset is pending, we need to signal
        // the Reset() call so it knows we've completed our poll and are
        // returning to sleep (i.e. the polling thread is now reset.)
        //
        pthread_mutex_lock(&polling_mutex); 
        if (_pollState == PollingState::InitRestart)
        {
            DEBUG_FAST("(T)MSCP Polling thread reset.");
            // Signal the Reset call that we're done so it can return
            // and release the Host.
            _pollState = PollingState::Wait;
            pthread_cond_signal(&polling_cond);
        }
        else if (_pollState == PollingState::InitRun)
        {
            _pollState = PollingState::Run;
        }
        else
        { 
            _pollState = PollingState::Wait;
        }
        pthread_mutex_unlock(&polling_mutex);
        
    }
    DEBUG_FAST("(T)MSCP Polling thread exiting."); 
}

uint32_t mscp_server_base::DispatchCommand(const std::shared_ptr<Message> message, const ControlMessageHeader* header, uint16_t modifiers, bool *protocolError)
{
    uint32_t cmdStatus = 0;
    switch (header->Word3.Command.Opcode)
    {
    case Opcodes::ABORT:
        cmdStatus = Abort();
        break;

    case Opcodes::ACCESS:
        cmdStatus = Access(message, header->UnitNumber);
        break;

    case Opcodes::AVAILABLE:
        cmdStatus = Available(header->UnitNumber, modifiers);
        break;

    case Opcodes::COMPARE_HOST_DATA:
        cmdStatus = CompareHostData(message, header->UnitNumber);
        break;

    case Opcodes::DETERMINE_ACCESS_PATHS:
        cmdStatus = DetermineAccessPaths(header->UnitNumber);
        break;

    case Opcodes::ERASE:
        cmdStatus = Erase(message, header->UnitNumber, modifiers);
        break;

    case Opcodes::GET_COMMAND_STATUS:
        cmdStatus = GetCommandStatus(message);
        break;

    case Opcodes::GET_UNIT_STATUS:
        cmdStatus = GetUnitStatus(message, header->UnitNumber, modifiers);
        break;

    case Opcodes::ONLINE:
        cmdStatus = Online(message, header->UnitNumber, modifiers);
        break;

    case Opcodes::READ:
        cmdStatus = Read(message, header->UnitNumber, modifiers);
        break;

    case Opcodes::SET_CONTROLLER_CHARACTERISTICS:
        cmdStatus = SetControllerCharacteristics(message);
        break;

    case Opcodes::SET_UNIT_CHARACTERISTICS:
        cmdStatus = SetUnitCharacteristics(message, header->UnitNumber, modifiers);
        break;

    case Opcodes::WRITE:
        cmdStatus = Write(message, header->UnitNumber, modifiers);
        break;

    default:
        DEBUG_FAST("Unimplemented (T)MSCP command 0x%x", header->Word3.Command.Opcode);
        *protocolError = true;
        break;
    }

    return cmdStatus;
}

//
// GetDrive():
//  Returns the mscp_drive_c object for the specified unit number,
//  or nullptr if no such object exists.
//
static mscp_drive_base_c *
GetDrive(
    uda_c * port,
    uint32_t unitNumber)
{
    mscp_drive_base_c* drive = nullptr;
    if (unitNumber < port->GetDriveCount())
    {
        drive = port->GetDrive(unitNumber);
    }

    return drive;
}

//
// The following are all implementations of the MSCP commands that are common between MSCP and TMSCP.
//
 
uint32_t
mscp_server_base::Abort()
{
    INFO("MSCP ABORT");

    //
    // Since we do not reorder messages and in fact pick up and execute
    // them one at a time, sequentially as they appear in the ring buffer,
    // by the time we've gotten this command, the command it's referring
    // to is long gone.
    // This is semi-legal behavior and it's legal for us to ignore ABORT in this
    // case.
    //
    // We just return SUCCESS here.
    return STATUS(Status::SUCCESS, 0, 0);
}

uint32_t
mscp_server_base::Available(
    uint16_t unitNumber,
    uint16_t modifiers)
{
    UNUSED(modifiers);

    // Message has no message-specific data.
    // Just set the specified drive as Available if appropriate.
    // We do nothing with the spin-down modifier.
    DEBUG_FAST("(T)MSCP AVAILABLE");

    mscp_drive_base_c* drive = GetDrive(_port, unitNumber);

    if (nullptr == drive ||
        !drive->IsAvailable())
    {
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }

    drive->SetOffline();

    return STATUS(Status::SUCCESS, 0x40, 0);  // still connected    
}

uint32_t
mscp_server_base::DetermineAccessPaths(
    uint16_t unitNumber)
{
    DEBUG_FAST("(T)MSCP DETERMINE ACCESS PATHS drive %d", unitNumber);

    // "This command must be treated as a no-op that always succeeds
    //  if the unit is incapable of being connected to more than one
    //  controller." That's us!

    mscp_drive_base_c* drive = GetDrive(_port, unitNumber); 
    if (nullptr == drive ||
        !drive->IsAvailable())
    {
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }
    else
    {
        return STATUS(Status::SUCCESS, 0, 0);
    }
}

uint32_t
mscp_server_base::GetCommandStatus(
    std::shared_ptr<Message> message)
{
    DEBUG_FAST("(T)MSCP GET COMMAND STATUS");

    #pragma pack(push,1)
    struct GetCommandStatusResponseParameters
    {
        uint32_t OutstandingReferenceNumber;
        uint32_t CommandStatus;
    };
    #pragma pack(pop)

    message->MessageLength = sizeof(GetCommandStatusResponseParameters)
        + HEADER_SIZE;

    GetCommandStatusResponseParameters* params = 
        reinterpret_cast<GetCommandStatusResponseParameters*>(
            GetParameterPointer(message));

    //
    // This will always return zero; as with the ABORT command, at this
    // point the command being referenced has already been executed.
    //
    params->CommandStatus = 0;

    return STATUS(Status::SUCCESS, 0, 0);
}

//
// GetParameterPointer():
//  Returns a pointer to the Parameter text in the given Message.
//
uint8_t*
mscp_server_base::GetParameterPointer(
    std::shared_ptr<Message> message)
{
    // We silence a strict aliasing warning here; this is safe (if perhaps not recommended
    // the general case.)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
    return reinterpret_cast<ControlMessageHeader*>(message->Message)->Parameters;
#pragma GCC diagnostic pop
}

//
// Reset():
//  Resets the MSCP server:
//   - Waits for the polling thread to finish its current work
//   - Releases all drives into the Available state
//
void 
mscp_server_base::Reset(void)
{
    DEBUG_FAST("Aborting polling due to reset.");

    pthread_mutex_lock(&polling_mutex);
    if (_pollState != PollingState::Wait)
    {
        _pollState = PollingState::InitRestart;

        while (_pollState != PollingState::Wait)
        {
            pthread_cond_wait(
                &polling_cond,
                &polling_mutex);
        }
    }  
    pthread_mutex_unlock(&polling_mutex);

    _credits = INIT_CREDITS;

    // Release all drives
    for (uint32_t i=0;i<_port->GetDriveCount();i++)
    {
        GetDrive(_port, i)->SetOffline();
    }
}

//
// InitPolling():
//  Wakes the polling thread.
//
void 
mscp_server_base::InitPolling(void)
{
    //
    // Wake the polling thread if not already awoken.
    //
    pthread_mutex_lock(&polling_mutex);
        DEBUG_FAST("Waking polling thread.");
        _pollState = PollingState::InitRun;
       	pthread_cond_signal(&polling_cond);
    pthread_mutex_unlock(&polling_mutex);
}

} // end namespace