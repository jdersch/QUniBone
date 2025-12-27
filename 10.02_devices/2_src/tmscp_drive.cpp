/*
 tmscp_drive.cpp: Implementation of TMSCP tapes.

 Copyright J. Dersch 2019-2026
 Contributed under the BSD 2-clause license.

 This provides logic for dealing with tape images contained in SIMH TAP-format containers.
 */

#include <assert.h>
#include <memory>

#include "logger.hpp"
#include "utils.hpp"
#include "tmscp_drive.hpp"
//#include "mscp_server.hpp"

tmscp_drive_c::tmscp_drive_c(storagecontroller_c *_controller, uint32_t _driveNumber) :
    storagedrive_c(_controller)
{
    set_workers_count(0) ; // needs no worker()
    log_label = "TMSCPD";
    SetDriveType("TU81");
    SetOffline();

    // Calculate the unit's ID:
    _unitDeviceNumber = _driveNumber + 1;
}

tmscp_drive_c::~tmscp_drive_c() 
{
    if (image_is_open()) {
        image_close();
    }
}

// on_param_changed():
//  Handles configuration parameter changes.
bool tmscp_drive_c::on_param_changed(parameter_c *param) 
{
    // no own "enable" logic
    if (&type_name == param) 
    {
        return SetDriveType(type_name.new_value.c_str());
    } 
    else if ( image_is_param(param)
                && image_recreate_on_param_change(param)
                && image_open(true) ) {
        // successfull created and opened the new image file.
        
        // TODO: update state here

        return true; // accept param
    } 

    return device_c::on_param_changed(param); // more actions (for enable)false;
}

//
// GetDeviceNumber():
//  Returns the unique device number for this drive.
//
uint32_t tmscp_drive_c::GetDeviceNumber() 
{
    return _unitDeviceNumber;
}

//
// GetClassModel():
//  Returns the class and model information for this drive.
//
uint16_t tmscp_drive_c::GetClassModel() 
{
    return _unitClassModel;
}

//
// IsAvailable():
//  Indicates whether this drive is available (i.e. has an image
//  assigned to it and can thus be used by the controller.)
//
bool tmscp_drive_c::IsAvailable() 
{
    return image_is_open();
}

//
// IsOnline():
//  Indicates whether this drive has been placed into an Online
//  state (for example by the ONLINE command).
//
bool tmscp_drive_c::IsOnline() 
{
    return _online;
}

//
// SetOnline():
//  Brings the drive online.
//
void tmscp_drive_c::SetOnline() 
{
    _online = true;

    //
    // Once online, the drive's type and image cannot be changed until
    // the drive is offline.
    //
    // type_name.readonly = true;
    // image_filepath.readonly = true;
}

//
// SetOffline():
//  Takes the drive offline.
//
void tmscp_drive_c::SetOffline() 
{
    _online = false;
    type_name.readonly = false;
    image_params_readonly(false) ;
}

//
// Writes the specified number of bytes from the provided buffer,
// starting at the specified logical block.
//
void tmscp_drive_c::Write(size_t lengthInBytes, uint8_t* buffer) 
{
    // TODO: make this happen
}

//
// Reads the specifed number of bytes starting at the specified logical
// block.  Returns a pointer to a buffer containing the data read.
// Caller is responsible for freeing this buffer.
//
uint8_t* tmscp_drive_c::Read(size_t lengthInBytes) 
{
    // TODO: yadda, yadda, yadda

    return nullptr;
}

//
//
// SetDriveType():
//  Updates this drive's type to the specified type (i.e.
//  TU81).
//  If the specified type is not found in our list of known
//  drive types, the drive's type is not changed and false
//  is returned.
//
bool tmscp_drive_c::SetDriveType(const char* typeName) 
{
    // TODO: implement
    return true;
}

//
// worker():
//  worker method for this drive.  No work is necessary.
//
//
// on_power_changed():
//  Handle power change notifications.
//
// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void tmscp_drive_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) 
{
    UNUSED(aclo_edge);
    UNUSED(dclo_edge);

    // Take the drive offline due to power change
    SetOffline();
}

//
// on_init_changed():
//  Handle INIT signal.
void tmscp_drive_c::on_init_changed(void) 
{
    // Take the drive offline due to reset
    SetOffline();
}

