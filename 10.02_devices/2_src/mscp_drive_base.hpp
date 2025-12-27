/*
 mscp_drive.hpp: Implementation of MSCP drive, used with MSCP controller.

 Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
 Contributed under the BSD 2-clause license.

 */

#pragma once

#include <stdint.h>
#include <string.h>
#include <memory>	// unique_ptr
#include "parameter.hpp"
#include "storagedrive.hpp"

//
// Implements the backing store for MSCP disk images
//
class mscp_drive_base_c: public storagedrive_c 
{
protected:
    mscp_drive_base_c(storagecontroller_c *controller, uint32_t driveNumber);

public:
    ~mscp_drive_base_c(void);

    bool on_param_changed(parameter_c *param) override;    

    void SetOnline(void);
    void SetOffline(void);
    bool IsOnline(void);
    bool IsAvailable(void);    

public:
    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
    void on_init_changed(void) override;

    bool _online;    
};
