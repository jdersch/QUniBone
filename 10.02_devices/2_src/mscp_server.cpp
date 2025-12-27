/*
    mscp_server.cpp: Implementation of a simple MSCP server.

    Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
    Copyright J. Dersch 2019-2026
    Contributed under the BSD 2-clause license.

    This provides an implementation of the Minimal MSCP subset outlined
    in AA-L619A-TK (Chapter 6).  It takes a few liberties and errs on 
    the side of implementation simplicity.

    In particular:
         All commands are executed sequentially, as they appear in the
         command ring.  This includes any commands in the "Immediate"
         category.  Technically this is incorrect:  Immediate commands
         should execute as soon as possible, before any other commands.
         In practice I have yet to find code that cares.

         This simplifies the implementation significantly, and apart
         from maintaining fealty to the MSCP spec for Immediate commands,
         there's no good reason to make it more complex:  real MSCP
         controllers (like the original UDA50) would resequence commands
         to allow optimal throughput across multiple units, etc.  On the
         Unibone, the underlying storage and the execution speed of the
         processor is orders of magnitude faster, so even a brute-force
         braindead implementation like this can saturate the Unibus.

    TODO:
    - Some commands aren't checked as thoroughly for errors as they could be.
    - Not all Invalid Command responses include the subcode data (which should,
      per section 5.5 of the MSCP spec, be the byte offset of the offending data
      in the invalid message.)  This is only really useful for diagnostic purposes
      and so the lack of it should not normally cause issues.
    - Same for the "flag" field, this is entirely unpopulated. 
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
#include "mscp_server.hpp"
#include "uda.hpp"

namespace mscp {

mscp_server::mscp_server(
    uda_c *port) :
        mscp_server_base(port),
        _hostTimeout(0),
        _controllerFlags(0)
{
    name.value = "mscp_server" ;
    type_name.value = "mscp_server_c";
    log_label = "MSSVR";
}


mscp_server::~mscp_server()
{

}

uint32_t mscp_server::DispatchCommand(const std::shared_ptr<Message> message, const ControlMessageHeader* header, uint16_t modifiers, bool *protocolError)
{
    uint32_t cmdStatus = 0;
    switch (header->Word3.Command.Opcode)
    {

    case Opcodes::REPLACE:
        cmdStatus = Replace(message, header->UnitNumber);
        break;

    default:
        // Run the base dispatch
        cmdStatus = mscp_server_base::DispatchCommand(message, header, modifiers, protocolError);
        break;
    }

    return cmdStatus;
}

//
// GetDrive():
//  Returns the mscp_drive_c object for the specified unit number,
//  or nullptr if no such object exists.
//
static mscp_drive_c*
GetDrive(
    uda_c * port,
    uint32_t unitNumber)
{    
    mscp_drive_c* drive = nullptr;
    if (unitNumber < port->GetDriveCount())
    {
        drive = dynamic_cast<mscp_drive_c*>(port->GetDrive(unitNumber));
    }

    return drive;
}

//
// The following are all implementations of the disk-specific MSCP commands we support.
//
 
uint32_t
mscp_server::Available(
    uint16_t unitNumber,
    uint16_t modifiers)
{
    UNUSED(modifiers);

    // Message has no message-specific data.
    // Just set the specified drive as Available if appropriate.
    // We do nothing with the spin-down modifier.
    DEBUG_FAST("MSCP AVAILABLE");

    mscp_drive_c* drive = GetDrive(_port, unitNumber);

    if (nullptr == drive ||
        !drive->IsAvailable())
    {
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }

    drive->SetOffline();

    return STATUS(Status::SUCCESS, 0x40, 0);  // still connected    
}

uint32_t
mscp_server::Access(
    std::shared_ptr<Message> message,
    uint16_t unitNumber)
{
    INFO("MSCP ACCESS");

    return DoDiskTransfer(
        Opcodes::ACCESS,
        message,
        unitNumber,
        0);
}

uint32_t
mscp_server::CompareHostData(
    std::shared_ptr<Message> message,
    uint16_t unitNumber)
{
    INFO("MSCP COMPARE HOST DATA");
    return DoDiskTransfer(
        Opcodes::COMPARE_HOST_DATA,
        message,
        unitNumber,
        0);
}

uint32_t
mscp_server::Erase(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    return DoDiskTransfer(
        Opcodes::ERASE,
        message,
        unitNumber,
        modifiers);
}

uint32_t
mscp_server::GetUnitStatus(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    #pragma pack(push,1)
    struct GetUnitStatusResponseParameters
    {
        uint16_t MultiUnitCode;
        uint16_t UnitFlags;
        uint32_t Reserved0;
        uint32_t UnitIdDeviceNumber;
        uint16_t UnitIdUnused;
        uint16_t UnitIdClassModel;
        uint32_t MediaTypeIdentifier;
        uint16_t ShadowUnit;
        uint16_t Reserved1;
        uint16_t TrackSize;
        uint16_t GroupSize;
        uint16_t CylinderSize;
        uint16_t Reserved2;   
        uint16_t RCTSize;
        uint8_t RBNs;
        uint8_t Copies;
    };
    #pragma pack(pop)

    DEBUG_FAST("MSCP GET UNIT STATUS drive %d", unitNumber);

    // Adjust message length for response
    message->MessageLength = sizeof(GetUnitStatusResponseParameters) +
        HEADER_SIZE;

    ControlMessageHeader* header =
        reinterpret_cast<ControlMessageHeader*>(message->Message);

    if (modifiers & 0x1)
    {
        // Next Unit modifier: return the next known unit >= unitNumber.
        // Unless unitNumber is greater than the number of drives we support
        // we just return the unit specified by unitNumber.
        if (unitNumber >= _port->GetDriveCount())
        {
            // In this case we act as if drive 0 was queried.
            unitNumber = 0;
            header->UnitNumber = 0;
        }
    }

    mscp_drive_c* drive = GetDrive(_port, unitNumber);

    GetUnitStatusResponseParameters* params = 
        reinterpret_cast<GetUnitStatusResponseParameters*>(
            GetParameterPointer(message));

    if (nullptr == drive || !drive->IsAvailable())
    {
        // No such drive or drive image not loaded.
        params->UnitIdDeviceNumber = 0;
        params->UnitIdClassModel = 0;
        params->UnitIdUnused = 0;
        params->ShadowUnit = 0;
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }

    params->Reserved0 = 0;
    params->Reserved1 = 0;
    params->Reserved2 = 0;
    params->UnitFlags = 0;  // TODO: 0 for now, which is sane.
    params->MultiUnitCode = 0; // Controller dependent, we don't support multi-unit drives.
    params->UnitIdDeviceNumber = drive->GetDeviceNumber();      
    params->UnitIdClassModel = drive->GetClassModel();
    params->UnitIdUnused = 0;
    params->MediaTypeIdentifier = drive->GetMediaID(); 
    params->ShadowUnit = unitNumber;   // Always equal to unit number
    
    // From the MSCP spec: "As stated above, the host area of  a  disk  is  structured  as  a
    //  vector of logical blocks.  From a performance viewpoint, however,
    //  it  is  more  appropriate  to  view  the  host  area  as  a  four
    //  dimensional hyper-cube."
    // This has nothing whatsoever to do with what's going on here but it makes me snicker
    // every time I read it so I'm including it.
    // Let's relay some information about our data-tesseract:
    // For older VMS, this has to match actual drive parameters.
    //
    params->TrackSize = drive->GetSectsPerTrack();
    params->GroupSize = drive->GetTracksPerGroup();
    params->CylinderSize = drive->GetGroupsPerCylinder();

    params->RCTSize = drive->GetRCTSize();
    params->RBNs = drive->GetRBNs();
    params->Copies = drive->GetRCTCopies();

    if (drive->IsOnline())
    {
        return STATUS(Status::SUCCESS, 0, 0);
    }
    else
    {
        return STATUS(Status::UNIT_AVAILABLE, 0, 0);
    } 
}

uint32_t
mscp_server::Online(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    #pragma pack(push,1)
    struct OnlineParameters
    {
        uint16_t UnitFlags alignas(2);
        uint16_t Reserved0 alignas(2);
        uint32_t Reserved1;
        uint32_t Reserved2;
        uint32_t Reserved3;
        uint32_t DeviceParameters;
        uint32_t Reserved4;
    };
    #pragma pack(pop)

    //
    // TODO: Right now, ignoring all incoming parameters.
    // With the exception of write-protection none of them really
    // apply.
    // We still need to flag errors if someone tries to set
    // host-settable flags we can't support.
    //

    // "The ONLINE command performs a SET UNIT CHARACTERISTICS
    // operation after bringing a unit 'Unit-Online'"
    return SetUnitCharacteristicsInternal(message, unitNumber, modifiers, true /*bring online*/);
}

uint32_t
mscp_server::Replace(
    std::shared_ptr<Message> message,
    uint16_t unitNumber)
{
    INFO("MSCP REPLACE");
    //
    // We treat this as a success for valid units as we do no block replacement at all.
    // Best just to smile and nod.  We could be more vigilant and check LBNs, etc...
    //
    message->MessageLength = HEADER_SIZE;

    mscp_drive_c* drive = GetDrive(_port, unitNumber);

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
mscp_server::SetControllerCharacteristics(
    std::shared_ptr<Message> message)
{
    #pragma pack(push,1)
    struct SetControllerCharacteristicsParameters
    {
        uint16_t MSCPVersion;    
        uint16_t ControllerFlags;
        uint16_t HostTimeout;
        uint16_t Reserved;
        union
        {
            uint64_t TimeAndDate;
            struct
            {
                uint32_t UniqueDeviceNumber;
                uint16_t Unused;
                uint16_t ClassModel;
            } ControllerId;
        } w;
    };
    #pragma pack(pop)
 
    SetControllerCharacteristicsParameters* params =
        reinterpret_cast<SetControllerCharacteristicsParameters*>(
            GetParameterPointer(message));

    DEBUG_FAST("MSCP SET CONTROLLER CHARACTERISTICS");

    // Adjust message length for response
    message->MessageLength = sizeof(SetControllerCharacteristicsParameters) +
        HEADER_SIZE;
    //
    // Check the version, if non-zero we must return an Invalid Command
    // end message.
    //
    if (params->MSCPVersion != 0)
    {
        return STATUS(Status::INVALID_COMMAND, 0, 0); // TODO: set sub-status
    }  
    else
    {
        _hostTimeout = params->HostTimeout;
        _controllerFlags = params->ControllerFlags; 

        // At this time we ignore the time and date entirely.
   
        // Prepare the response message 
        params->Reserved = 0;
        params->ControllerFlags = _controllerFlags & 0xfe;  // Mask off 576 byte sectors bit.
                                                            // it's read-only and we're a 512
                                                            // byte sector shop here. 
        params->HostTimeout = 0xff;   // Controller timeout: return the max value.
        params->w.ControllerId.UniqueDeviceNumber = _port->GetControllerIdentifier();
        params->w.ControllerId.ClassModel = _port->GetControllerClassModel();
        params->w.ControllerId.Unused = 0;

        return STATUS(Status::SUCCESS, 0, 0);
    }
     
}

uint32_t
mscp_server::SetUnitCharacteristics(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    #pragma pack(push,1)
    struct SetUnitCharacteristicsParameters
    {
        uint16_t UnitFlags;
        uint16_t Reserved0;
        uint32_t Reserved1;
        uint64_t Reserved2;
        uint32_t DeviceDependent;
        uint16_t Reserved3;
        uint16_t Reserved4;
    };
    #pragma pack(pop)

    // TODO: handle Set Write Protect modifier

    DEBUG_FAST("MSCP SET UNIT CHARACTERISTICS drive %d", unitNumber);

    return SetUnitCharacteristicsInternal(message, unitNumber, modifiers, false);
}


uint32_t
mscp_server::Read(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    return DoDiskTransfer(
        Opcodes::READ,
        message,
        unitNumber,
        modifiers);
}

uint32_t
mscp_server::Write(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    return DoDiskTransfer(
        Opcodes::WRITE,
        message,
        unitNumber,
        modifiers);
}

//
// SetUnitCharacteristicsInternal():
//  Logic common to both ONLINE and SET UNIT CHARACTERISTICS commands.
//
uint32_t
mscp_server::SetUnitCharacteristicsInternal(
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers,
    bool bringOnline)
{
    UNUSED(modifiers);
    // TODO: handle Set Write Protect modifier

    #pragma pack(push,1)
    struct SetUnitCharacteristicsResponseParameters
    {
        uint16_t UnitFlags;
        uint16_t MultiUnitCode;
        uint32_t Reserved0;
        uint32_t UnitIdDeviceNumber;
        uint16_t UnitIdUnused;
        uint16_t UnitIdClassModel;
        uint32_t MediaTypeIdentifier;
        uint32_t Reserved1;
        uint32_t UnitSize;
        uint32_t VolumeSerialNumber;
    };
    #pragma pack(pop)

    // Adjust message length for response
    message->MessageLength = sizeof(SetUnitCharacteristicsResponseParameters) +
        HEADER_SIZE;

    mscp_drive_c* drive = GetDrive(_port, unitNumber);
    // Check unit
    if (nullptr == drive ||
        !drive->IsAvailable())
    {
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }

    SetUnitCharacteristicsResponseParameters* params =
        reinterpret_cast<SetUnitCharacteristicsResponseParameters*>(
            GetParameterPointer(message));

    params->UnitFlags = 0;  // TODO: 0 for now, which is sane.
    params->MultiUnitCode = 0; // Controller dependent, we don't support multi-unit drives.
    params->UnitIdDeviceNumber = drive->GetDeviceNumber();
    params->UnitIdClassModel = drive->GetClassModel();
    params->UnitIdUnused = 0;
    params->MediaTypeIdentifier = drive->GetMediaID();
    params->UnitSize = drive->GetBlockCount();
    params->VolumeSerialNumber = 0;
    params->Reserved0 = 0;
    params->Reserved1 = 0;

    if (bringOnline)
    {
        bool alreadyOnline = drive->IsOnline();
        drive->SetOnline();
        return STATUS(Status::SUCCESS,  
            (alreadyOnline ? SuccessSubcodes::ALREADY_ONLINE : SuccessSubcodes::NORMAL), 0); 
    }
    else
    {
        return STATUS(Status::SUCCESS, 0, 0);
    }
}

//
// DoDiskTransfer():
//  Common transfer logic for READ, WRITE, ERASE, COMPARE HOST DATA and ACCCESS commands.
//
uint32_t
mscp_server::DoDiskTransfer(
    uint16_t operation,
    std::shared_ptr<Message> message,
    uint16_t unitNumber,
    uint16_t modifiers)
{
    #pragma pack(push,1)
    struct ReadWriteEraseParameters
    {
        uint32_t ByteCount;
        uint32_t BufferPhysicalAddress;  // upper 8 bits are channel address for VAXen
        uint32_t Unused0;
        uint32_t Unused1;
        uint32_t LBN;
    };
    #pragma pack(pop)

    ReadWriteEraseParameters* params =
        reinterpret_cast<ReadWriteEraseParameters*>(GetParameterPointer(message));

    DEBUG_FAST("MSCP RWE 0x%x unit %d mod 0x%x chan o%o pa o%o count %d lbn %d",
        operation,
        unitNumber,
        modifiers,
        params->BufferPhysicalAddress >> 24,
        params->BufferPhysicalAddress & 0x00ffffff,
        params->ByteCount,
        params->LBN);

    // Adjust message length for response
    message->MessageLength = sizeof(ReadWriteEraseParameters) +
        HEADER_SIZE;

    mscp_drive_c* drive = GetDrive(_port, unitNumber);

    // Check unit
    if (nullptr == drive ||
        !drive->IsAvailable())
    {
        return STATUS(Status::UNIT_OFFLINE, UnitOfflineSubcodes::UNIT_UNKNOWN, 0);
    }

    if (!drive->IsOnline())
    {
        return STATUS(Status::UNIT_AVAILABLE, 0, 0);
    }

    // Are we accessing the RCT area?
    bool rctAccess = params->LBN >= drive->GetBlockCount(); 
    uint32_t rctBlockNumber = params->LBN - drive->GetBlockCount();

    // Check that the LBN is valid
    if (params->LBN >= drive->GetBlockCount() + drive->GetRCTBlockCount())
    {
        uint16_t subCode = offsetof(ReadWriteEraseParameters, LBN) + HEADER_OFFSET;
        return STATUS(Status::INVALID_COMMAND, subCode, 0);
    }

    // Check byte count:  
    if (params->ByteCount > ((drive->GetBlockCount() + drive->GetRCTBlockCount()) - params->LBN) * drive->GetBlockSize())
    {
        uint16_t subCode = offsetof(ReadWriteEraseParameters, ByteCount) + HEADER_OFFSET;
        return STATUS(Status::INVALID_COMMAND, subCode, 0);
    }

    // If this is an RCT access, byte count must equal the block size.
    if (rctAccess && params->ByteCount != drive->GetBlockSize())
    {
        uint16_t subCode = offsetof(ReadWriteEraseParameters, ByteCount) + HEADER_OFFSET;
        return STATUS(Status::INVALID_COMMAND, subCode, 0);
    }

    //
    // OK: do the transfer from the PDP-11 to a buffer
    //
    switch (operation)
    {
        case Opcodes::ACCESS:
            // We don't need to actually do any sort of transfer; ACCESS merely checks
            // That the data can be read -- we checked the LBN, etc. above and we 
            // will never encounter a read error, so there's nothing left to do.
        break;

        case Opcodes::COMPARE_HOST_DATA:
        {
            // Read the data in from disk, read the data in from memory, and compare.
            std::unique_ptr<uint8_t> diskBuffer;

            if (rctAccess)
            {
                diskBuffer.reset(drive->ReadRCTBlock(rctBlockNumber));
            }
            else
            {
                diskBuffer.reset(drive->Read(params->LBN, params->ByteCount));
            }

            std::unique_ptr<uint8_t> memBuffer(_port->DMARead(
                params->BufferPhysicalAddress & 0x00ffffff,
                params->ByteCount,
                params->ByteCount));
 
            if (!memBuffer)
            {
                return STATUS(Status::HOST_BUFFER_ACCESS_ERROR, HostBufferAccessSubcodes::NXM, 0);
            }
  
            if (!memcmp(diskBuffer.get(), memBuffer.get(), params->ByteCount))
            {
                return STATUS(Status::COMPARE_ERROR, 0, 0);
            }
        }
 
        case Opcodes::ERASE:
        {
            std::unique_ptr<uint8_t> memBuffer(new uint8_t[params->ByteCount]);
            memset(reinterpret_cast<void*>(memBuffer.get()), 0, params->ByteCount);

            if (rctAccess)
            {
                drive->WriteRCTBlock(rctBlockNumber,
                    memBuffer.get());
            }
            else
            {
                drive->Write(params->LBN,
                    params->ByteCount,
                    memBuffer.get());
            }
        } 
        break;

        case Opcodes::READ:
        {
            std::unique_ptr<uint8_t> diskBuffer;
        
            if (rctAccess)
            {
                diskBuffer.reset(drive->ReadRCTBlock(rctBlockNumber));
            }
            else
            { 
                diskBuffer.reset(drive->Read(params->LBN, params->ByteCount));
            }

            if (!_port->DMAWrite(
                params->BufferPhysicalAddress & 0x00ffffff,
                params->ByteCount,
                diskBuffer.get()))
            {
                return STATUS(Status::HOST_BUFFER_ACCESS_ERROR, HostBufferAccessSubcodes::NXM, 0);
            }

        }
        break;

        case Opcodes::WRITE:
        {
            std::unique_ptr<uint8_t> memBuffer(_port->DMARead(
                params->BufferPhysicalAddress & 0x00ffffff,
                params->ByteCount,
                params->ByteCount));

            if (!memBuffer)
            {
                return STATUS(Status::HOST_BUFFER_ACCESS_ERROR, HostBufferAccessSubcodes::NXM, 0);
            }
 
            if (rctAccess)
            {
                drive->WriteRCTBlock(rctBlockNumber,
                    memBuffer.get());
            }
            else
            {
                drive->Write(params->LBN,
                    params->ByteCount,
                    memBuffer.get());
            }
        }
        break;

        default:
            // Should never happen.
            assert(false);
            break;
    }

    // Set parameters for response.
    // We leave ByteCount as is (for now anyway)
    // And set First Bad Block to 0.  (This is unnecessary since we're
    // not reporting a bad block, but we're doing it for completeness.)
    params->LBN = 0;

    return STATUS(Status::SUCCESS, 0, 0);
}

} // end namespace