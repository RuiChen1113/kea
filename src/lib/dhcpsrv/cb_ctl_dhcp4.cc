// Copyright (C) 2019 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <config.h>
#include <dhcpsrv/cb_ctl_dhcp4.h>
#include <dhcpsrv/cfgmgr.h>
#include <dhcpsrv/dhcpsrv_log.h>

using namespace isc::data;
using namespace isc::process;

namespace isc {
namespace dhcp {

void
CBControlDHCPv4::databaseConfigApply(const db::BackendSelector& backend_selector,
                                     const db::ServerSelector& server_selector,
                                     const boost::posix_time::ptime& lb_modification_time,
                                     const db::AuditEntryCollection& audit_entries) {
    // Create the external config into which we'll fetch backend config data.
    SrvConfigPtr external_cfg = CfgMgr::instance().createExternalCfg();

    // First let's fetch the globals and add them to external config.
    if (fetchConfigElement(audit_entries, "dhcp4_global_parameter")) {
        data::StampedValueCollection globals;
        globals = getMgr().getPool()->getModifiedGlobalParameters4(backend_selector, server_selector,
                                                                   lb_modification_time);
        addGlobalsToConfig(external_cfg, globals);
    }

    // Now we fetch the option definitions and add them.
    if (fetchConfigElement(audit_entries, "dhcp4_option_def")) {
        OptionDefContainer option_defs =
            getMgr().getPool()->getModifiedOptionDefs4(backend_selector, server_selector,
                                                       lb_modification_time);
        for (auto option_def = option_defs.begin(); option_def != option_defs.end(); ++option_def) {
            external_cfg->getCfgOptionDef()->add((*option_def), (*option_def)->getOptionSpaceName());
        }
    }

    // Next fetch the options. They are returned as a container of OptionDescriptors.
    if (fetchConfigElement(audit_entries, "dhcp4_options")) {
        OptionContainer options = getMgr().getPool()->getModifiedOptions4(backend_selector,
                                                                          server_selector,
                                                                          lb_modification_time);
        for (auto option = options.begin(); option != options.end(); ++option) {
            external_cfg->getCfgOption()->add((*option), (*option).space_name_);
        }
    }

    // Now fetch the shared networks.
    if (fetchConfigElement(audit_entries, "dhcp4_shared_network")) {
        SharedNetwork4Collection networks =
            getMgr().getPool()->getModifiedSharedNetworks4(backend_selector, server_selector,
                                                           lb_modification_time);
        for (auto network = networks.begin(); network != networks.end(); ++network) {
            // In order to take advantage of the dynamic inheritance of global
            // parameters to a shared network we need to set a callback function
            // for each network to allow for fetching global parameters.
            (*network)->setFetchGlobalsFn([] () -> ConstElementPtr {
                return (CfgMgr::instance().getCurrentCfg()->getConfiguredGlobals());
            });
            external_cfg->getCfgSharedNetworks4()->add((*network));
        }
    }

    // Next we fetch subnets.
    if (fetchConfigElement(audit_entries, "dhcp4_subnet")) {
        Subnet4Collection subnets = getMgr().getPool()->getModifiedSubnets4(backend_selector,
                                                                            server_selector,
                                                                            lb_modification_time);
        for (auto subnet = subnets.begin(); subnet != subnets.end(); ++subnet) {
            // In order to take advantage of the dynamic inheritance of global
            // parameters to a subnet we need to set a callback function for each
            // subnet to allow for fetching global parameters.
            (*subnet)->setFetchGlobalsFn([] () -> ConstElementPtr {
                return (CfgMgr::instance().getCurrentCfg()->getConfiguredGlobals());
            });
            external_cfg->getCfgSubnets4()->add((*subnet));
        }
    }

    if (audit_entries.empty()) {
        CfgMgr::instance().mergeIntoStagingCfg(external_cfg->getSequence());

    } else {
        CfgMgr::instance().mergeIntoCurrentCfg(external_cfg->getSequence());
    }
    LOG_INFO(dhcpsrv_logger, DHCPSRV_CFGMGR_CONFIG4_MERGED);
}


} // end of namespace isc::dhcp
} // end of namespace isc
