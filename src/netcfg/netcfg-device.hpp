//  OpenVPN 3 Linux client -- Next generation OpenVPN client
//
//  Copyright (C) 2018         OpenVPN, Inc. <sales@openvpn.net>
//  Copyright (C) 2018         David Sommerseth <davids@openvpn.net>
//  Copyright (C) 2018         Arne Schwabe <arne@openvpn.net>
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
 * @file   netcfg-device.hpp
 *
 * @brief  D-Bus object representing a single virtual network device
 *         the net.openvpn.v3.netcfg service manages
 */

#pragma once

#include <functional>
#include <gio-unix-2.0/gio/gunixfdlist.h>
#include <gio-unix-2.0/gio/gunixconnection.h>

#include <openvpn/common/rc.hpp>

#include "dbus/core.hpp"
#include "dbus/connection-creds.hpp"
#include "dbus/glibutils.hpp"
#include "dbus/object-property.hpp"
#include "common/lookup.hpp"
#include "core-tunbuilder.hpp"
#include "./dns-direct-file.hpp"
#include "netcfg-options.hpp"
#include "netcfg-stateevent.hpp"
#include "netcfg-signals.hpp"

using namespace openvpn;
using namespace NetCfg;

enum NetCfgDeviceType : unsigned int
{
    UNSET = 0,   // Primarily to avoid 0 but still have 0 defined
    TAP   = 2,
    TUN   = 3
            // Expliclity use 3 for tun and 2 for tap as 2==TUN would be very
            // confusing
};


class IPAddr {
public:
    IPAddr() = default;
    IPAddr(std::string ipaddr, bool ipv6)
        : address(std::move(ipaddr)), ipv6(ipv6)
    {
    }

    std::string address;
    bool ipv6;
};

/**
 * Class representing a IPv4 or IPv6 network
 */
class Network : public IPAddr {
public:
    Network(std::string networkAddress, unsigned int prefix,
            bool ipv6, bool exclude=false) :
            IPAddr(networkAddress, ipv6),
            prefix(prefix), exclude(exclude)
    {
    }

    unsigned int prefix;
    bool exclude;
};

class VPNAddress: public Network {
public:
    VPNAddress(std::string networkAddress, unsigned int prefix,
               std::string gateway, bool ipv6):
               Network(networkAddress, prefix, ipv6, false),
               gateway(std::move(gateway))
    {
    }

    std::string gateway;
};

class NetCfgDevice : public DBusObject,
                     public DBusCredentials
{
    friend CoreTunbuilderImpl;

public:
    NetCfgDevice(GDBusConnection *dbuscon,
                 std::function<void()> remove_callback,
                 const uid_t creator, const std::string& objpath,
                 std::string devname,
                 DNS::ResolverSettings *resolver,
                 const unsigned int log_level, LogWriter *logwr,
                 NetCfgOptions options)
        : DBusObject(objpath),
          DBusCredentials(dbuscon, creator),
          remove_callback(std::move(remove_callback)),
          properties(this),
          device_name(devname),
          mtu(1500), txqueuelen(0),
          signal(dbuscon, LogGroup::NETCFG, objpath, logwr),
          resolver(resolver),
          options(std::move(options))
    {
        signal.SetLogLevel(log_level);

        properties.AddBinding(new PropertyType<std::string>(this, "device_name", "read", false, device_name));
        properties.AddBinding(new PropertyType<decltype(dns_servers)>(this, "dns_servers", "read", false, dns_servers));
        properties.AddBinding(new PropertyType<decltype(dns_search)>(this, "dns_search", "read", false, dns_search));
        properties.AddBinding(new PropertyType<unsigned int>(this, "mtu", "readwrite", false, mtu));
        properties.AddBinding(new PropertyType<unsigned int>(this, "layer", "readwrite", false, device_type));
        properties.AddBinding(new PropertyType<unsigned int>(this, "txqueuelen", "readwrite", false, txqueuelen));
        properties.AddBinding(new PropertyType<bool>(this, "reroute_ipv4", "readwrite", false, reroute_ipv4));
        properties.AddBinding(new PropertyType<bool>(this, "reroute_ipv6", "readwrite", false, reroute_ipv6));


        std::stringstream introspect;
        introspect << "<node name='" << objpath << "'>"
                   << "    <interface name='" << OpenVPN3DBus_interf_netcfg << "'>"
                   << "        <method name='AddIPAddress'>"
                   << "            <arg direction='in' type='s' name='ip_address'/>"
                   << "            <arg direction='in' type='u' name='prefix'/>"
                   << "            <arg direction='in' type='s' name='gateway'/>"
                   << "            <arg direction='in' type='b' name='ipv6'/>"
                   << "        </method>"
                   << "        <method name='SetRemoteAddress'>"
                   << "            <arg direction='in' type='s' name='ip_address'/>"
                   << "            <arg direction='in' type='b' name='ipv6'/>"
                   << "        </method>"
                   << "        <method name='AddNetworks'>"
                   << "            <arg direction='in' type='a(subb)' name='networks'/>"
                   << "        </method>"
                   << "        <method name='AddDNS'>"
                   << "            <arg direction='in' type='as' name='server_list'/>"
                   << "        </method>"
                   << "        <method name='AddDNSSearch'>"
                   << "            <arg direction='in' type='as' name='domains'/>"
                   << "        </method>"
                   << "        <method name='RemoveDNSSearch'>"
                   << "            <arg direction='in' type='as' name='domains'/>"
                   << "        </method>"
                   << "        <method name='Establish'/>"
                                /* Note: Although Establish returns a unix_fd,
                                 * it does not belong in the method
                                 * signature, since glib/dbus abstraction is
                                 * paper thin and it is handled almost like
                                 * in recv/sendmsg as auxiliary data
                                 */
                   << "        <method name='Disable'/>"
                   << "        <method name='Destroy'/>"
                   << "        <property type='u'  name='log_level' access='readwrite'/>"
                   << "        <property type='u'  name='owner' access='read'/>"
                   << "        <property type='au' name='acl' access='read'/>"
                   << "        <property type='b'  name='active' access='read'/>"
                   << "        <property type='b'  name='modified' access='read'/>"
                   << properties.GetIntrospectionXML()
                   << signal.GetLogIntrospection()
                   << NetCfgStateEvent::IntrospectionXML()
                   << "    </interface>"
                   << "</node>";
        ParseIntrospectionXML(introspect);
        signal.LogVerb2("Network device '" + devname + "' prepared");

        // Increment the device reference counter in the resolver
        if (resolver)
        {
            resolver->IncDeviceCount();
        }
    }

    ~NetCfgDevice()
    {
        remove_callback();
        IdleCheck_RefDec();
    }
private:

    void addIPAddress(GVariant* params)
    {
        GLibUtils::checkParams(__func__, params, "(susb)", 4);

        std::string ipaddr(g_variant_get_string(g_variant_get_child_value(params, 0), 0));
        uint32_t prefix = g_variant_get_uint32(g_variant_get_child_value(params, 1));
        std::string gateway(g_variant_get_string(g_variant_get_child_value(params, 2), 0));
        bool ipv6 = g_variant_get_boolean(g_variant_get_child_value(params, 3));

        signal.LogInfo(std::string("Adding IP Adress ") + ipaddr
                       + "/" + std::to_string(prefix)
                       + " gw " + gateway + " ipv6: " + (ipv6 ? "yes" : "no"));

        vpnips.emplace_back(VPNAddress(std::string(ipaddr), prefix,
                                       std::string(gateway), ipv6));
    }

    void setRemoteAddress(GVariant* params)
    {

        GLibUtils::checkParams(__func__, params, "(sb)", 2);

        std::string ipaddr(g_variant_get_string(g_variant_get_child_value(params, 0), 0));
        bool ipv6 = g_variant_get_boolean(g_variant_get_child_value(params, 1));

        signal.LogInfo(std::string("Setting remote IP address to '") + ipaddr +
                                    " ipv6: " + (ipv6 ? "yes" : "no"));
        remote = IPAddr(std::string(ipaddr), ipv6);
    }

    void addNetworks(GVariant* params)
    {
        GLibUtils::checkParams(__func__, params, "(a(subb))", 1);
        GVariantIter* network_iter;
        g_variant_get(params, "(a(subb))", &network_iter);

        GVariant *network = nullptr;
        while ((network = g_variant_iter_next_value(network_iter)))
        {
            GLibUtils::checkParams(__func__, network, "(subb)", 4);

            std::string net(g_variant_get_string(g_variant_get_child_value(network, 0), 0));
            uint32_t prefix = g_variant_get_uint32(g_variant_get_child_value(network, 1));
            bool ipv6 = g_variant_get_boolean(g_variant_get_child_value(network, 2));
            bool exclude = g_variant_get_boolean(g_variant_get_child_value(network, 3));

            signal.LogInfo(std::string("Adding network '") + net + "/"
                           + std::to_string(prefix)
                           + "' excl: " + (exclude ? "yes" : "no")
                           + " ipv6: " + (ipv6 ? "yes" : "no"));

            networks.emplace_back(Network(std::string(net), prefix,
                                          ipv6, exclude));
        }
        g_variant_iter_free(network_iter);

    }


public:
    /**
     *  Callback method which is called each time a D-Bus method call occurs
     *  on this BackendClientObject.
     *
     * @param conn        D-Bus connection where the method call occurred
     * @param sender      D-Bus bus name of the sender of the method call
     * @param obj_path    D-Bus object path of the target object.
     * @param intf_name   D-Bus interface of the method call
     * @param method_name D-Bus method name to be executed
     * @param params      GVariant Glib2 object containing the arguments for
     *                    the method call
     * @param invoc       GDBusMethodInvocation where the response/result of
     *                    the method call will be returned.
     */
    void callback_method_call(GDBusConnection *conn,
                              const std::string sender,
                              const std::string obj_path,
                              const std::string intf_name,
                              const std::string method_name,
                              GVariant *params,
                              GDBusMethodInvocation *invoc)
    {
        try
        {
            IdleCheck_UpdateTimestamp();

            // Only the VPN backend clients are granted access
            validate_sender(sender);

            GVariant *retval = nullptr;
            if ("AddIPAddress" == method_name)
            {
                // Adds a single IPv4 address to the virtual device.  If
                // broadcast has not been provided, calculate it if needed.
                addIPAddress(params);
             }
            else if ("AddNetworks" == method_name)
            {
                // The caller sends an array of networks to apply
                // It is an array, as this makes everything happen in a
                // single D-Bus method call and it can on some hosts
                // be a considerable amount of routes.  This speeds up
                // the execution
                //
                // The variable signature is not completely decided and
                // must be adopted to what is appropriate
                addNetworks(params);
             }
            else if ("SetRemoteAddress" == method_name)
            {
                setRemoteAddress(params);
            }
            else if ("AddDNS" == method_name)
            {
                if (!resolver)
                {
                    throw NetCfgException("No resolver configured");
                }

                // Adds DNS servers
                auto added = resolver->AddDNSServers(params);

                // Keep track of DNS servers provided by this interface
                dns_servers.insert(dns_servers.end(),
                                   std::make_move_iterator(added.begin()),
                                   std::make_move_iterator(added.end()));
             }
            else if ("RemoveDNS" == method_name)
            {
                if (!resolver)
                {
                    throw NetCfgException("No resolver configured");
                }

                // Removes DNS servers
                auto removed = resolver->RemoveDNSServers(params);

                // Remove local tracking of DNS servers provided by
                // this interface
                for (const auto& e : removed)
                {
                    dns_servers.erase(std::remove(dns_servers.begin(),
                                                  dns_servers.end(),
                                                  e.c_str()),
                                      dns_servers.end());

                }
             }
            else if ("AddDNSSearch" == method_name)
            {
                if (!resolver)
                {
                    throw NetCfgException("No resolver configured");
                }

                // Adds DNS search domains
                auto added = resolver->AddDNSSearch(params);

                // Keep track of DNS search domains added by this interface
                dns_search.insert(dns_search.end(),
                                  std::make_move_iterator(added.begin()),
                                  std::make_move_iterator(added.end()));
            }
            else if ("RemoveDNSSearch" == method_name)
            {
                if (!resolver)
                {
                    throw NetCfgException("No resolver configured");
                }

                // Removes DNS search domains
                auto removed = resolver->RemoveDNSSearch(params);

                // Remove local tracking of DNS search domains provided
                // by this interface
                for (const auto& e : removed)
                {
                    dns_search.erase(std::remove(dns_search.begin(),
                                                 dns_search.end(), e.c_str()),
                                    dns_search.end());
                }
            }
            else if ("Establish" == method_name)
            {
                // This should generally be true for DBus 1.3,
                // double checking here cannot hurt
                g_assert(g_dbus_connection_get_capabilities(conn) & G_DBUS_CAPABILITY_FLAGS_UNIX_FD_PASSING);

                // The virtual device has not yet been created on the host,
                // but all settings which has been queued up will be activated
                // when this method is called.
                if (resolver && resolver->GetModified())
                {
                    resolver->Apply();
                }
                if (!tunimpl)
                {
                    tunimpl.reset(getCoreBuilderInstance());
                }
                int fd = tunimpl->establish(*this);

                GUnixFDList *fdlist;
                GError *error = nullptr;
                fdlist = g_unix_fd_list_new();
                g_unix_fd_list_append(fdlist, fd, &error);
                close(fd);

                if(error)
                {
                    throw NetCfgException("Creating fd list failed");
                }

                // DBus will close the handle on our side after transmitting
                g_dbus_method_invocation_return_value_with_unix_fd_list(invoc, nullptr, fdlist);
                GLibUtils::unref_fdlist(fdlist);
                return;
            }
            else if ("Disable" == method_name)
            {
                if (false) // FIXME
                {
                    // This tears down and disables a virtual device but
                    // enables the device to be re-activated again with the
                    // same settings by calling the 'Activate' method again

                    // Only restore the resolv.conf file if this is the last
                    // device using these ResolverSettings object
                    if (resolver && resolver->GetDeviceCount() <= 1)
                    {
                        try
                        {
                            resolver->Restore();
                        }
                        catch (const NetCfgException& excp)
                        {
                            signal.LogCritical(excp.what());
                        }
                    }
                }
                if (tunimpl)
                {
                    tunimpl->teardown(true);
                    tunimpl.reset();
                }
            }
            else if ("Destroy" == method_name)
            {
                // This should run 'Disable' if this has not happened
                // and then this object is completely deleted

                CheckOwnerAccess(sender);

                if (resolver)
                {
                    resolver->DecDeviceCount();
                    if (resolver->GetDeviceCount() == 0)
                    {
                        try
                        {
                            resolver->Restore();
                        }
                        catch (const NetCfgException& excp)
                        {
                            signal.LogCritical(excp.what());
                        }
                    }
                }

                std::string sender_name = lookup_username(GetUID(sender));
                signal.LogVerb1("Device '" + device_name + "' was removed by "
                               + sender_name);
                RemoveObject(conn);
                if (tunimpl) {
                    tunimpl->teardown(true);
                    tunimpl.reset();
                }

                g_dbus_method_invocation_return_value(invoc, nullptr);
                delete this;
                return;
            }
            else
            {
                throw NetCfgException("Called method " + method_name + " unknown");
            }
            g_dbus_method_invocation_return_value(invoc, retval);
            return;
        }
        catch (DBusCredentialsException& excp)
        {
            signal.LogCritical(excp.err());
            excp.SetDBusError(invoc);
        }
        catch (const std::exception& excp)
        {
            std::string errmsg = "Failed executing D-Bus call '"
                                  + method_name + "': " + excp.what();
            GError *err = g_dbus_error_new_for_dbus_error("net.openvpn.v3.netcfg.error.generic",
                                                          errmsg.c_str());
            g_dbus_method_invocation_return_gerror(invoc, err);
            g_error_free(err);
        }
        catch (...)
        {
            GError *err = g_dbus_error_new_for_dbus_error("net.openvpn.v3.netcfg.error.unspecified",
                                                          "Unknown error");
            g_dbus_method_invocation_return_gerror(invoc, err);
            g_error_free(err);
        }
    }


    /**
     *   Callback which is used each time a NetCfgServiceObject D-Bus property
     *   is being read.
     *
     * @param conn           D-Bus connection this event occurred on
     * @param sender         D-Bus bus name of the requester
     * @param obj_path       D-Bus object path to the object being requested
     * @param intf_name      D-Bus interface of the property being accessed
     * @param property_name  The property name being accessed
     * @param error          A GLib2 GError object if an error occurs
     *
     * @return  Returns a GVariant Glib2 object containing the value of the
     *          requested D-Bus object property.  On errors, NULL must be
     *          returned and the error must be returned via a GError
     *          object.
     */
    GVariant * callback_get_property(GDBusConnection *conn,
                                     const std::string sender,
                                     const std::string obj_path,
                                     const std::string intf_name,
                                     const std::string property_name,
                                     GError **error)
    {
        try
        {
            IdleCheck_UpdateTimestamp();
            validate_sender(sender);

            if ("log_level" == property_name)
            {
                return g_variant_new_uint32(signal.GetLogLevel());
            }
            else if ("owner" == property_name)
            {
                return GetOwner();
            }
            else if ("acl" == property_name)
            {
                return GetAccessList();
            }
            else if ("active" == property_name)
            {
                return g_variant_new_boolean(active);
            }
            else if ("modified" == property_name)
            {
                bool modified = false;
                if (resolver)
                {
                    modified |= resolver->GetModified();
                }
                return g_variant_new_boolean(modified);
            }
            else if (properties.Exists(property_name))
            {
                return properties.GetValue(property_name);
            }
        }
        catch (DBusPropertyException&)
        {
            throw;
        }
        catch (const NetCfgException & excp)
        {
            throw DBusPropertyException(G_IO_ERROR, G_IO_ERROR_FAILED,
                                        intf_name, obj_path, property_name,
                                        excp.what());
        }
        catch (const DBusException& excp)
        {
            throw DBusPropertyException(G_IO_ERROR, G_IO_ERROR_FAILED,
                                        intf_name, obj_path, property_name,
                                        excp.what());
        }
        throw DBusPropertyException(G_IO_ERROR, G_IO_ERROR_FAILED,
                                    obj_path, intf_name, property_name,
                                    "Invalid property");
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                    "Unknown property");
        return nullptr;
    }


    /**
     *  Callback method which is used each time a NetCfgServiceObject
     *  property is being modified over the D-Bus.
     *
     * @param conn           D-Bus connection this event occurred on
     * @param sender         D-Bus bus name of the requester
     * @param obj_path       D-Bus object path to the object being requested
     * @param intf_name      D-Bus interface of the property being accessed
     * @param property_name  The property name being accessed
     * @param value          GVariant object containing the value to be stored
     * @param error          A GLib2 GError object if an error occurs
     *
     * @return Returns a GVariantBuilder object containing the change
     *         confirmation on success.  On failures, an exception is thrown.
     *
     */
    GVariantBuilder * callback_set_property(GDBusConnection *conn,
                                            const std::string sender,
                                            const std::string obj_path,
                                            const std::string intf_name,
                                            const std::string property_name,
                                            GVariant *value,
                                            GError **error)
    {
        try
        {
            IdleCheck_UpdateTimestamp();
            validate_sender(sender);

            if ("log_level" == property_name)
            {
                unsigned int log_level = g_variant_get_uint32(value);
                if (log_level > 6)
                {
                    throw DBusPropertyException(G_IO_ERROR,
                                                G_IO_ERROR_INVALID_DATA,
                                                obj_path, intf_name,
                                                property_name,
                                                "Invalid log level");
                }
                signal.SetLogLevel(log_level);
                return build_set_property_response(property_name,
                                                   (guint32) log_level);
            }
            else if (properties.Exists(property_name))
            {
                return properties.SetValue(property_name, value);
            }
        }
        catch (DBusPropertyException&)
        {
            throw;
        }
        catch (DBusException& excp)
        {
            throw DBusPropertyException(G_IO_ERROR, G_IO_ERROR_FAILED,
                                        obj_path, intf_name, property_name,
                                        excp.what());
        }
        throw DBusPropertyException(G_IO_ERROR, G_IO_ERROR_FAILED,
                                    obj_path, intf_name, property_name,
                                    "Invalid property");
    }




private:
    std::function<void()> remove_callback;

    // Properties
    PropertyCollection properties;
    unsigned int device_type = NetCfgDeviceType::UNSET;
    std::string device_name;
    std::vector<std::string> dns_servers;
    std::vector<std::string> dns_search;
    std::vector<Network> networks;
    std::vector<VPNAddress> vpnips;
    IPAddr remote;
    unsigned int mtu;
    unsigned int txqueuelen;

    bool reroute_ipv4 = false;
    bool reroute_ipv6 = false;


    RCPtr<CoreTunbuilder> tunimpl;
    NetCfgSignals signal;
    DNS::ResolverSettings * resolver = nullptr;
    NetCfgOptions options;
    bool active = false;


    /**
     *  Validate that the sender is allowed to do change the configuration
     *  for this device.  If not, a DBusCredentialsException is thrown.
     *
     * @param sender  String containing the unique bus ID of the sender
     */
    void validate_sender(std::string sender)
    {
        return;  // FIXME: Currently disabled

        // Only the session manager is susposed to talk to the
        // the backend VPN client service
        if (GetUniqueBusID(OpenVPN3DBus_name_sessions) != sender)
        {
            throw DBusCredentialsException(GetUID(sender),
                                           "net.openvpn.v3.error.acl.denied",
                                           "You are not a session manager"
                                           );
        }
    }
};

