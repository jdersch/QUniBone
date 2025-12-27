/*
    tmscp_server.hpp: Implementation of a simple TMSCP server.

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
// 
//
class tmscp_server : public mscp_server_base
{
public:
    tmscp_server(uda_c *port);
    ~tmscp_server();

protected:
    uint32_t DispatchCommand(const std::shared_ptr<Message> message, const ControlMessageHeader* header, uint16_t modifiers, bool *protocolError) override;

private:
    // TMSCP-specific implementations:
    uint32_t Access(std::shared_ptr<Message> message, uint16_t unitNumber) override;
    uint32_t Available(uint16_t unitNumber, uint16_t modifiers) override;
    uint32_t CompareHostData(std::shared_ptr<Message> message, uint16_t unitNumber) override;
    uint32_t Erase(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers) override;
    uint32_t GetUnitStatus(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers) override;
    uint32_t Online(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers) override;
    uint32_t Read(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers) override;
    uint32_t SetControllerCharacteristics(std::shared_ptr<Message> message) override;
    uint32_t SetUnitCharacteristics(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers) override;
    uint32_t Write(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers) override;

private:
    // Commands unique to TMSCP
    uint32_t EraseGap(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers);
    uint32_t Reposition(std::shared_ptr<Message> message, uint16_t unitNumber);
    uint32_t WriteTapeMark(std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers);

private:
    tmscp_drive_c* GetDrive(uint32_t unitNumber);

};

} // end namespace
