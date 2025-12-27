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

//
// Implements the backing store for TMSCP tape images (SIMH TAP format)
//
class tmscp_drive_c : public storagedrive_c 
{
public:
    tmscp_drive_c(storagecontroller_c *controller, uint32_t driveNumber);
    ~tmscp_drive_c(void);

    bool on_param_changed(parameter_c *param) override;

    uint32_t GetDeviceNumber(void);
    uint16_t GetClassModel(void);

    void SetOnline(void);
    void SetOffline(void);
    bool IsOnline(void);
    bool IsAvailable(void);
    void Position(size_t index);
    void Rewind();
    void WriteMark();  //TODO: flesh out
    void Write(size_t lengthInBytes, uint8_t* buffer);
    uint8_t* Read(size_t lengthInBytes);


public:
    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override;
    void on_init_changed(void) override;

private:
    bool SetDriveType(const char* typeName);

private:

    bool _online;
    uint32_t _unitDeviceNumber;
    uint16_t _unitClassModel;
};
