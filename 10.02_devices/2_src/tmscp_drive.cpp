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

tmscp_drive_c::tmscp_drive_c(storagecontroller_c* controller, uint32_t driveNumber) :
    mscp_drive_base_c(controller, driveNumber)
{
    log_label = "TMSCPD";
    SetDriveType("TU81");

    // Calculate the unit's ID:
    _unitDeviceNumber = driveNumber + 1;
}

tmscp_drive_c::~tmscp_drive_c() 
{    
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

