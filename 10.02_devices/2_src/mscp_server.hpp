/*
    mscp_server.hpp: Implementation of a simple MSCP server.

    Copyright Vulcan Inc. 2019 via Living Computers: Museum + Labs, Seattle, WA.
    Copyright J. Dersch 2019-2026
    Contributed under the BSD 2-clause license.
*/

#pragma once

#include <stdint.h>
#include <memory>
#include "mscp_server_base.hpp"

namespace mscp {

//
//
//
class mscp_server : public mscp_server_base
{
public:
    mscp_server(uda_c *port);
    ~mscp_server();

protected:
    uint32_t DispatchCommand(const std::shared_ptr<Message> message, const ControlMessageHeader* header, uint16_t modifiers, bool *protocolError) override;

private:
    // MSCP-specific implementations
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
    // Commands unique to MSCP
    uint32_t Replace(std::shared_ptr<Message> message, uint16_t unitNumber);

private:
    uint32_t SetUnitCharacteristicsInternal(
        std::shared_ptr<Message> message,
        uint16_t unitNumber,
        uint16_t modifiers,
        bool bringOnline);
    uint32_t DoDiskTransfer(uint16_t operation, std::shared_ptr<Message> message, uint16_t unitNumber, uint16_t modifiers);

private:
    uint32_t _hostTimeout;
    uint32_t _controllerFlags;

};

} // end namespace

