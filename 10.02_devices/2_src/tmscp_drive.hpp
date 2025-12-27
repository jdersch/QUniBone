/*
 tmscp_drive.hpp: Implementation of TMSCP drive, used with TMSCP controller.

 Copyright 2025 Josh Dersch
 Contributed under the BSD 2-clause license.

 */

#pragma once

#include <stdint.h>
#include <string.h>
#include <memory>	// unique_ptr
#include "parameter.hpp"
#include "storagedrive.hpp"
#include "mscp_drive_base.hpp"

//
// Implements the backing store for TMSCP tape images (SIMH TAP format)
//
class tmscp_drive_c : public mscp_drive_base_c 
{
public:
    tmscp_drive_c(storagecontroller_c *controller, uint32_t driveNumber);
    ~tmscp_drive_c(void);

    bool on_param_changed(parameter_c *param) override;

    uint32_t GetDeviceNumber(void);
    uint16_t GetClassModel(void);
    
    void Position(size_t index);
    void Rewind();
    void WriteMark();  //TODO: flesh out
    void Write(size_t lengthInBytes, uint8_t* buffer);
    uint8_t* Read(size_t lengthInBytes);

private:
    bool SetDriveType(const char* typeName);

private:

    uint32_t _unitDeviceNumber;
    uint16_t _unitClassModel;
};
