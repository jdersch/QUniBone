/*
 mscp_drive.cpp: Implementation of MSCP disks.

 Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
 Contributed under the BSD 2-clause license.

 This provides the logic for reads and writes to the data and RCT space
 for a given drive, as well as configuration for different standard DEC
 drive types.

 Disk data is backed by an image file on disk.  RCT data exists only in
 memory and is not saved -- it is provided to satisfy software that
 expects the RCT area to exist.  Since no bad sectors will ever actually
 exist, the RCT area has no real purpose, so it is ephemeral in this
 implementation.
 */

#include <assert.h>
#include <memory>

#include "logger.hpp"
#include "utils.hpp"
#include "mscp_drive_base.hpp"

mscp_drive_base_c::mscp_drive_base_c(storagecontroller_c *_controller, uint32_t _driveNumber) :
    storagedrive_c(_controller)
{
    set_workers_count(0) ; // needs no worker()    
    SetOffline();    
}

mscp_drive_base_c::~mscp_drive_base_c() 
{
    if (image_is_open()) {
        image_close();
    }
}

// on_param_changed():
//  Handles configuration parameter changes.
bool mscp_drive_base_c::on_param_changed(parameter_c *param) 
{    
    return device_c::on_param_changed(param); // more actions (for enable)false;
}

//
// IsAvailable():
//  Indicates whether this drive is available (i.e. has an image
//  assigned to it and can thus be used by the controller.)
//
bool mscp_drive_base_c::IsAvailable() 
{
    return image_is_open();
}

//
// IsOnline():
//  Indicates whether this drive has been placed into an Online
//  state (for example by the ONLINE command).
//
bool mscp_drive_base_c::IsOnline() 
{
    return _online;
}

//
// SetOnline():
//  Brings the drive online.
//
void mscp_drive_base_c::SetOnline() 
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
void mscp_drive_base_c::SetOffline() 
{
    _online = false;
    type_name.readonly = false;
    image_params_readonly(false);
}

//
// on_power_changed():
//  Handle power change notifications.
//
// after QBUS/UNIBUS install, device is reset by DCLO/DCOK cycle
void mscp_drive_base_c::on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) 
{
    UNUSED(aclo_edge);
    UNUSED(dclo_edge);
    // Take the drive offline due to power change
    SetOffline();
}

//
// on_init_changed():
//  Handle INIT signal.
void mscp_drive_base_c::on_init_changed(void) 
{
    // Take the drive offline due to reset
    SetOffline();
}

