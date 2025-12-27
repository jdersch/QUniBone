/*
    tmscp_server.hpp: Implementation of a simple TMSCP server, subclass of MSCP server

    Copyright 2025 J. Dersch
    Contributed under the BSD 2-clause license.
*/

#pragma once

#include <stdint.h>
#include <memory>
#include "mscp_server.hpp"
#include "uda.hpp"

namespace mscp
{

//
// The TMSCP server implementation subclasses the MSCP server implementation, overriding
//  DispatchCommand to handle TMSCP commands as necessary.
//
class tmscp_server : public mscp_server
{
public:
    tmscp_server(uda_c *port);
    ~tmscp_server();
	bool on_param_changed(parameter_c *param) override;

public:
    void Reset(void) override;

public:
    void on_power_changed(signal_edge_enum aclo_edge, signal_edge_enum dclo_edge) override {
        UNUSED(aclo_edge) ;
        UNUSED(dclo_edge) ;
        }
    void on_init_changed(void) override {}

protected:
    void SetMetadata() override;
    uint32_t DispatchCommand(const std::shared_ptr<Message> message, const ControlMessageHeader* header, uint16_t modifiers, bool *protocolError) override;


private:
    tmscp_drive_c* GetDrive(uint32_t unitNumber);

};

} // end namespace
