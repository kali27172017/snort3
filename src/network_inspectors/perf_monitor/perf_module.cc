//--------------------------------------------------------------------------
// Copyright (C) 2014-2016 Cisco and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

// perf_module.cc author Russ Combs <rucombs@cisco.com>

#include "perf_module.h"

#include "managers/module_manager.h"
#include "managers/plugin_manager.h"
#include "utils/util.h"

//-------------------------------------------------------------------------
// perf attributes
//-------------------------------------------------------------------------

static const Parameter module_params[] =
{
    { "name", Parameter::PT_STRING, nullptr, nullptr,
      "name of the module" },

    { "pegs", Parameter::PT_STRING, nullptr, nullptr,
      "list of statistics to track or empty for all counters" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr },
};

static const Parameter s_params[] =
{
    { "packets", Parameter::PT_INT, "0:", "10000",
      "minimum packets to report" },

    { "seconds", Parameter::PT_INT, "1:", "60",
      "report interval" },

    { "flow_ip_memcap", Parameter::PT_INT, "8200:", "52428800",
      "maximum memory for flow tracking" },

    { "max_file_size", Parameter::PT_INT, "4096:", "1073741824",
      "files will be rolled over if they exceed this size" },

    { "flow_ports", Parameter::PT_INT, "0:", "1023",
      "maximum ports to track" },

    { "reset", Parameter::PT_BOOL, nullptr, "true",
      "reset (clear) statistics after each reporting interval" },

#ifndef LINUX_SMP
    { "max", Parameter::PT_BOOL, nullptr, "false",
      "calculate theoretical maximum performance" },
#endif

    { "console", Parameter::PT_BOOL, nullptr, "false",
      "output to console" },

    { "events", Parameter::PT_BOOL, nullptr, "false",
      "report on qualified vs non-qualified events" },

    { "file", Parameter::PT_BOOL, nullptr, "false",
      "output base stats to " BASE_FILE " instead of stdout" },

    { "flow", Parameter::PT_BOOL, nullptr, "false",
      "enable traffic statistics" },

    { "flow_file", Parameter::PT_BOOL, nullptr, "false",
      "output traffic statistics to a " FLOW_FILE " instead of stdout" },

    { "flow_ip", Parameter::PT_BOOL, nullptr, "false",
      "enable statistics on host pairs" },

    { "flow_ip_file", Parameter::PT_BOOL, nullptr, "false",
      "output host pair statistics to " FLIP_FILE " instead of stdout" },

    { "modules", Parameter::PT_LIST, module_params, nullptr,
      "gather statistics from the specified modules" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr }
};

//-------------------------------------------------------------------------
// perf attributes
//-------------------------------------------------------------------------

PerfMonModule::PerfMonModule() :
    Module(PERF_NAME, PERF_HELP, s_params)
{ }

ProfileStats* PerfMonModule::get_profile() const
{ return &perfmonStats; }

bool PerfMonModule::set(const char*, Value& v, SnortConfig*)
{
    if ( v.is("packets") )
        config.pkt_cnt = v.get_long();

    else if ( v.is("seconds") )
    {
        config.sample_interval = v.get_long();
        if ( config.sample_interval == 0 )
        {
            config.perf_flags |= SFPERF_SUMMARY;
            config.perf_flags &= ~SFPERF_TIME_COUNT;
        }
    }
    else if ( v.is("flow_ip_memcap") )
    {
        config.flowip_memcap = v.get_long();
    }
    else if ( v.is("max_file_size") )
        config.max_file_size = v.get_long() - ROLLOVER_THRESH;

    else if ( v.is("flow_ports") )
    {
        config.flow_max_port_to_track = v.get_long();
    }
    else if ( v.is("reset") )
        config.base_reset = v.get_bool();

#ifndef LINUX_SMP
    else if ( v.is("max") )
    {
        if ( v.get_bool() )
            config.perf_flags |= SFPERF_MAX_BASE_STATS;
    }
#endif
    else if ( v.is("console") )
    {
        if ( v.get_bool() )
            config.perf_flags |= SFPERF_CONSOLE;
    }
    else if ( v.is("events") )
    {
        if ( v.get_bool() )
            config.perf_flags |= SFPERF_EVENT;
    }
    else if ( v.is("file") )
        config.file = v.get_bool();
    else if ( v.is("flow") )
    {
        if ( v.get_bool() )
            config.perf_flags |= SFPERF_FLOW;
    }
    else if ( v.is("flow_file") )
    {
        if ( v.get_bool() )
        {
            config.perf_flags |= SFPERF_FLOW;
            config.flow_file = true;
        }
    }
    else if ( v.is("flow_ip") )
    {
        if ( v.get_bool() )
            config.perf_flags |= SFPERF_FLOWIP;
    }
    else if ( v.is("flow_ip_file") )
    {
        if ( v.get_bool() )
        {
            config.perf_flags |= SFPERF_FLOWIP;
            config.flowip_file = true;
        }
    }
    else if ( v.is("name") )
    {
        mod_name = v.get_string();
    }
    else if ( v.is("pegs") )
    {
        mod_pegs = v.get_string();
        return true;
    }
    else
        return false;

    return true;
}

bool PerfMonModule::begin(const char* fqn, int, SnortConfig*)
{
    if ( !strcmp(fqn, "perf_monitor.modules") )
    {
        mod_name.clear();
        mod_pegs.clear();
    }
    else
    {
        memset(&config, 0, sizeof(SFPERF));
        config.perf_flags |= SFPERF_BASE | SFPERF_TIME_COUNT;
    }
    return true;
}

static bool add_module(SFPERF& config, Module *mod, std::string pegs)
{
    const PegInfo* peg_info;
    std::string tok;
    Value v(pegs.c_str());
    unsigned t_count = 0;

    if ( !mod )
        return false;

    config.modules.push_back(mod);
    config.mod_peg_idxs.push_back(std::vector<unsigned>());

    peg_info = mod->get_pegs();

    for ( v.set_first_token(); v.get_next_token(tok); t_count++ )
    {
        bool found = false;

        for ( int i = 0; peg_info[i].name; i++ )
        {
            if ( !strcmp(tok.c_str(), peg_info[i].name) )
            {
                config.mod_peg_idxs.back().push_back(i);
                found = true;
                break;
            }
        }
        if ( !found )
            return false;
    }
    if ( !t_count && peg_info )
        for ( int i = 0; peg_info[i].name; i++ )
            config.mod_peg_idxs.back().push_back(i);
    return true;
}

bool PerfMonModule::end(const char* fqn, int idx, SnortConfig*)
{
    if ( !idx )
    {
        if ( !config.modules.size() )
        {
            auto modules = ModuleManager::get_all_modules();
            for ( auto& mod : modules )
            {
                if ( !add_module(config, mod, std::string()) )
                    return false;
            }
        }
        return true;
    }

    if ( !strcmp(fqn, "perf_monitor.modules") && mod_name.size() > 0 )
        return add_module(config, ModuleManager::get_module(mod_name.c_str()), mod_pegs);

    return true;
}

void PerfMonModule::get_config(SFPERF& cfg)
{
    cfg = config;
    memset(&config, 0, sizeof(config));
}

const PegInfo* PerfMonModule::get_pegs() const
{ return simple_pegs; }

PegCount* PerfMonModule::get_counts() const
{ return (PegCount*)&pmstats; }

