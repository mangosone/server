/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2025 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

#ifndef MANGOS_H_GMTICKETMGR
#define MANGOS_H_GMTICKETMGR

#include "Policies/Singleton.h"
#include "Database/DatabaseEnv.h"
#include "Util.h"
#include "ObjectGuid.h"
#include "WorldSession.h"
#include "SharedDefines.h"
#include <map>

enum GMTicketResponse
{
    GMTICKET_RESPONSE_ALREADY_EXIST     = 1,
    GMTICKET_RESPONSE_CREATE_SUCCESS    = 2,
    GMTICKET_RESPONSE_CREATE_ERROR      = 3,
    GMTICKET_RESPONSE_UPDATE_SUCCESS    = 4,
    GMTICKET_RESPONSE_UPDATE_ERROR      = 5,
    GMTICKET_RESPONSE_TICKET_DELETED    = 9
};

/**
 * \addtogroup game
 * @{
 * \file
 */

/**
 * This is the class that takes care of representing a ticket made to the GMs on the server
 * with a question of some sort.
 *
 * The code responsible for taking care of the opcodes coming
 * in can be found in:
 * - \ref WorldSession::SendGMTicketStatusUpdate
 * - \ref WorldSession::SendGMTicketGetTicket
 * - \ref WorldSession::HandleGMTicketGetTicketOpcode
 * - \ref WorldSession::HandleGMTicketUpdateTextOpcode
 * - \ref WorldSession::HandleGMTicketDeleteTicketOpcode
 * - \ref WorldSession::HandleGMTicketCreateOpcode
 * - \ref WorldSession::HandleGMTicketSystemStatusOpcode
 * - \ref WorldSession::HandleGMTicketSurveySubmitOpcode
 * These in their turn will make calls to the \ref GMTicketMgr which will take
 * care of what needs to be done by giving back a \ref GMTicket. The database table interesting
 * in this case is character_ticket in the characaters database.
 *
 * Theres also some handling of tickets in \ref ChatHandler::HandleTicketAcceptCommand where
 * you can turn on/off accepting tickets with your current GM char. You can also turn
 * off tickets globally, this will show the client a message about tickets not being
 * available at the moment. The commands that can be used are:
 * <dl>
 * <dt>.ticket meaccept on/off</dt>
 * <dd>Turns on/off showing new incoming tickets for you character</dd>
 * <dt>.ticket accept on/off</dt>
 * <dd>Will turn the whole ticket reporting system on/off, ie: if it's off the clients
 * will get a message that the system is unavailable when trying to submit a ticket</dd>
 * <dt>.ticket close $character_name/.ticket close #num_of_ticket</dt>
 * <dd>Will close a ticket for the given character name or the given number of the ticket,
 * this will make the little icon in the top right go away for the player</dd>
 * <dt>.ticket surveyclose $character_name/.ticket surveyclose #num_of_ticket</dt>
 * <dd>Does the same as .ticket close but instead of just closing it it also asks the \ref Player
 * to answer a survey about how please they were with the experience</dd>
 * <dt>.ticket respond $character_name/.ticket respond #num_of_ticket</dt>
 * <dd>Will respond to a ticket, this will whisper the \ref Player who asked the question and from
 * there on you will have to explain the solution etc. and then close the ticket again.</dd>
 * <dt>.ticket info</dt>
 * <dd>Shows the number of currently active tickets</dd>
 * <dt>.ticket show $character_name/.ticket show #num_of_ticket</dt>
 * <dd>Will show the question and name of the character for the given ticket</dd>
 *
 * \todo Log conversations between GM and the player receiving help.
 */
class GMTicket
{
    public:
        explicit GMTicket() : m_guid(), m_ticketId(0), m_text(), m_responseText(), m_lastUpdate(0)
        {}

        /**
         * Initializes this \ref GMTicket, much like the constructor would.
         * @param guid guid for the \ref Player that created the ticket
         * @param text the question text
         * @param responsetext the response to the question if any
         * @param update the last time the ticket was updated by either \ref Player or GM
         */
        void Init(ObjectGuid guid, const std::string& text, const std::string& responseText, time_t update, uint32 ticketId);

        /**
         * Gets the \ref Player s \ref ObjectGuid which asked the question and created the ticket
         * @return the \ref ObjectGuid for the \ref Player that asked the question
         */
        ObjectGuid const& GetPlayerGuid() const
        {
            return m_guid;
        }
        /**
         * Get the tickets question
         * @return the question this ticket had
         */
        const char* GetText() const
        {
            return m_text.c_str();
        }
        /**
         * Get the response given to this ticket, if any
         * @return the response that was made to this tickets question
         */
        const char* GetResponse() const
        {
            return m_responseText.c_str();
        }
        /**
         * Tells us when the last update was done as a UNIX timestamp.
         * @return Time since last update in seconds since UNIX epoch
         */
        uint64 GetLastUpdate() const
        {
            return m_lastUpdate;
        }
        /**
         * Gets the id for this \ref GMTicket, as represented in the database
         * table characters.character_ticket
         * @return id for this ticket in the database
         */
        uint32 GetId() const
        {
            return m_ticketId;
        }

        /**
         * Changes the tickets question text.
         * @param text the text to change the question to
         */
        void SetText(const char* text);
        /**
         * Changes the response to the ticket
         * @param text the response to give
         * \deprecated
         */
        void SetResponseText(const char* text);

        /**
         * Has this ticket gotten a response?
         * @return true if there's some kind of response to this ticket, false otherwise
         * \deprecated
         * \todo Change to resolved/not resolved instead, via the check in db
         */
        bool HasResponse() { return !m_responseText.empty(); }

        /**
         * This will take care of a \ref OpcodesList::CMSG_GMSURVEY_SUBMIT packet
         * and save the data received into the database, this is not implemented yet
         * @param recvData the packet we received with answers to the survey
         * \todo Implement saving this to DB
         */
        void SaveSurveyData(WorldPacket& recvData) const;
        /**
         * Close this ticket so that the window showing in the client for the \ref Player
         * disappears.
         */
        void Close() const;
        /**
         * This closes the ticket aswell, but this is called when the client itself closed it
         * because they figured out the solution to their question
         */
        void CloseByClient() const;
        /**
         * This does the same thing as \ref GMTicket::Close but it also shows a survey window to the
         * \ref Player so that they can answer how well the GM behaved and such.
         * \todo Save the survey results in DB!
         */
        void CloseWithSurvey() const;
    private:
        void _Close(GMTicketStatus statusCode) const;

        ObjectGuid m_guid; ///< The GUID of the player who created the ticket
        uint32 m_ticketId; ///< The ID of the ticket in the database
        std::string m_text; ///< The question text of the ticket
        std::string m_responseText; ///< The response text to the ticket
        time_t m_lastUpdate; ///< The last update time of the ticket
};
typedef std::map<ObjectGuid, GMTicket> GMTicketMap;
typedef std::map<uint32, GMTicket*> GMTicketIdMap;                  // for creating order access

/**
 * This class manages all GM tickets on the server.
 */
class GMTicketMgr
{
    public:
        //TODO: Make the default value a config option instead
        GMTicketMgr() : m_TicketSystemOn(true), m_GMTicketMap(), m_GMTicketIdMap()
        {  }
        ~GMTicketMgr() {  }

        /**
         * Loads all GM tickets from the database.
         */
        void LoadGMTickets();

        /**
         * Retrieves a GM ticket by the player's GUID.
         * @param guid The GUID of the player who created the ticket
         * @return A pointer to the GM ticket, or NULL if not found
         */
        GMTicket* GetGMTicket(ObjectGuid guid)
        {
            GMTicketMap::iterator itr = m_GMTicketMap.find(guid);
            if (itr == m_GMTicketMap.end())
            {
                return NULL;
            }
            return &(itr->second);
        }

        /**
         * Retrieves a GM ticket by its ID.
         * @param id The ID of the ticket
         * @return A pointer to the GM ticket, or NULL if not found
         */
        GMTicket* GetGMTicket(uint32 id)
        {
            GMTicketIdMap::iterator itr = m_GMTicketIdMap.find(id);
            if (itr == m_GMTicketIdMap.end())
            {
                return NULL;
            }
            return itr->second;
        }

        /**
         * Gets the total number of GM tickets.
         * @return The number of GM tickets
         */
        size_t GetTicketCount() const
        {
            return m_GMTicketMap.size();
        }

        /**
         * Retrieves a GM ticket by its order position.
         * @param pos The order position of the ticket
         * @return A pointer to the GM ticket, or NULL if not found
         */
        GMTicket* GetGMTicketByOrderPos(uint32 pos)
        {
            if (pos >= GetTicketCount())
            {
                return NULL;
            }

            GMTicketMap::iterator itr = m_GMTicketMap.begin();
            std::advance(itr, pos);
            if (itr == m_GMTicketMap.end())
            {
                return NULL;
            }
            return &(itr->second);
        }

        /**
         * Deletes a GM ticket by the player's GUID.
         * Note: This will not remove anything from the database.
         * @param guid The GUID of the player who created the ticket
         */
        void Delete(ObjectGuid guid)
        {
            GMTicketMap::iterator itr = m_GMTicketMap.find(guid);
            if (itr == m_GMTicketMap.end())
            {
                return;
            }
            m_GMTicketIdMap.erase(itr->second.GetId());
            m_GMTicketMap.erase(itr);
        }

        /**
         * Deletes all GM tickets.
         */
        void DeleteAll();

        /**
         * Creates a new GM ticket.
         * This function will close any other currently open tickets for the given player and open a new one with the given text.
         * @param guid The GUID of the player who created the ticket
         * @param text The question text of the ticket
         */
        void Create(ObjectGuid guid, const char* text);

        /**
         * Turns on/off accepting tickets globally.
         * If this is off, the client will see a message telling them that filing tickets is currently unavailable.
         * @param accept True to accept tickets, false to not accept tickets
         */
        void SetAcceptTickets(bool accept) { m_TicketSystemOn = accept; }
        /**
         * Checks if we accept tickets globally.
         * @return True if we are accepting tickets globally, false otherwise
         * \todo Perhaps rename to IsAcceptingTickets?
         */
        bool WillAcceptTickets() const { return m_TicketSystemOn; }
    private:
        bool m_TicketSystemOn; ///< Whether the ticket system is on or off
        GMTicketMap m_GMTicketMap; ///< Map of player GUIDs to GM tickets
        GMTicketIdMap m_GMTicketIdMap; ///< Map of ticket IDs to GM tickets
};

#define sTicketMgr MaNGOS::Singleton<GMTicketMgr>::Instance()

/** @} */
#endif
