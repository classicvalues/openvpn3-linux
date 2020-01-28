//  OpenVPN 3 Linux client -- Next generation OpenVPN client
//
//  Copyright (C) 2019 - 2020  OpenVPN Inc <sales@openvpn.net>
//  Copyright (C) 2019 - 2020  David Sommerseth <davids@openvpn.net>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU Affero General Public License as
//  published by the Free Software Foundation, version 3 of the
//  License.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU Affero General Public License for more details.
//
//  You should have received a copy of the GNU Affero General Public License
//  along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

/**
 * @file   resolver-settings.hpp
 *
 *
 * @brief  Declares the class which is responsible for managing DNS
 *         resolver settings
 */
#pragma once

#include <sstream>
#include <vector>

#include <openvpn/common/rc.hpp>

using namespace openvpn;

namespace NetCfg
{
namespace DNS
{
    class ResolverSettings : public virtual RC<thread_unsafe_refcount>
    {
    public:
        typedef RCPtr<ResolverSettings> Ptr;

        ResolverSettings(ssize_t idx);
        ~ResolverSettings();

        ResolverSettings(const ResolverSettings::Ptr& orig)
        {
            index = orig->index;
            enabled = orig->enabled;
            name_servers = orig->name_servers;
            search_domains = orig->search_domains;
        }

        /**
         *  Retrieves the sorting index of this particular resolver settings
         *  object.  The sorting index is used to preserve a specific order
         *  of how resolver settings are applied on the system.
         *
         * @return  Returns an ssize_t value of the current index
         */
        ssize_t GetIndex() const noexcept;

        /**
         *  Set the activation flag for this ResolverSettings object
         *  to true.
         *
         *  The resolver-backend will use this flag to understand
         *  if these settings should be applied or not to the system.
         */
        void Enable() noexcept;

        /**
         *  Set the activation flag for this ResolverSettings object
         *  to false.
         *
         *  The resolver-backend will use this flag to understand
         *  if these settings should be applied or not to the system.
         */
        void Disable() noexcept;


        /**
         *  Retrieve the activation status of this settings object
         *
         *  The resolver-backend will use this flag to understand
         *  if these settings should be applied or not to the system.
         *
         * @return  Returns true if this ResolverSettings object has its
         *          changes enabled.  Otherwise false is returned.
         */
        bool GetEnabled() const noexcept;


        /**
         *  Check if there are resolver changes available.  If neither
         *  DNS resolver servers or search domains has been set, it will
         *  return false.
         *
         * @return  Returns true if there are changes present, otherwise false
         */
        bool ChangesAvailable() const noexcept;


        /**
         *  Adds a new single DNS name server
         *
         * @param server  std::string of DNS server to enlist
         */
        void AddNameServer(const std::string& server);

        /**
         *  Clear the list of DNS name servers
         */
        void ClearNameServers();

        /**
         *  Retrieve the current list of DNS name servers
         *
         * @return  Returns a std::vector<std::string> of all registered
         *          DNS name servers
         */
        std::vector<std::string> GetNameServers() const noexcept;

        /**
         *  Adds a new single DNS search domain
         *
         * @param server  std::string of DNS search domain to add
         */
        void AddSearchDomain(const std::string& domain);

        /**
         *  Clear the list of DNS search domains
         */
        void ClearSearchDomains();

        /**
         *  Retrieve the current list of DNS search domains
         *
         * @return  Returns a std::vector<std::string> of all registered
         *          DNS search domains
         */
        std::vector<std::string> GetSearchDomains() const noexcept;


        /**
         *  Makes it possible to write ResolverSettings in a readable format
         *  via iostreams, such as 'std::cout << rs', where rs is a
         *  ResolverSettings object.
         *
         * @param os  std::ostream where to write the data
         * @param rs  ResolverSettings object to write to the stream
         *
         * @return  Returns the provided std::ostream together with the
         *          decoded ResolverSertings information
         */
        friend std::ostream& operator<<(std::ostream& os,
                                        const ResolverSettings& rs)
        {
            if (rs.name_servers.empty() && rs.search_domains.empty())
            {
                return os << "(No DNS resolver settings)";
            }

            if (!rs.enabled)
            {
                return os << "(Settings not enabled)";
            }
            std::stringstream s;
            if (rs.name_servers.size() > 0)
            {
                s << "DNS resolvers: ";
                bool first = true;
                for (const auto& r : rs.name_servers)
                {
                    s << (!first ? ", " : "") << r;
                    first = false;
                }
            }
            if (rs.search_domains.size() > 0)
            {
                if (s.str().size() > 0)
                {
                    s << " - ";
                }
                s << "Search domains: ";
                bool first = true;
                for (const auto& sd : rs.search_domains)
                {
                    s << (!first ? ", " : "") << sd;
                    first = false;
                }
            }
            return os << s.str();
        }


#ifdef __GIO_TYPES_H__  // Only add GLib/GDBus methods if this is already used
        /**
         *  Adds DNS name servers based on an array of strings provided via
         *  a GVariant container of the (as) type.
         *
         * @param params  GVariant object containing an (as) based string
         *                array of elements to process
         *
         * @returns Returns a std::string list of added DNS servers,
         *          comma separated
         */
        const std::string AddNameServers(GVariant *params);

        /**
         *  Adds new DNS search domains based on an array of strings provided
         *  via a GVariant container of the (as) type.
         *
         * @param params  GVariant object containing an (as) based string
         *                array of elements to process
         */
        void AddSearchDomains(GVariant *params);
#endif


    private:
        ssize_t index = -1;
        bool enabled = false;
        std::vector<std::string> name_servers;
        std::vector<std::string> search_domains;
    };
}
}
