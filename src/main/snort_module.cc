/*
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

// snort_module.cc author Russ Combs <rucombs@cisco.com>

#include "snort_module.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <string.h>

#include <string>
using namespace std;

#include "main.h"
#include "snort.h"
#include "help.h"
#include "shell.h"
#include "snort_config.h"
#include "detection/detect.h"
#include "framework/module.h"
#include "framework/parameter.h"
#include "managers/module_manager.h"
#include "parser/config_file.h"
#include "parser/parser.h"
#include "parser/vars.h"
#include "packet_io/trough.h"

#ifdef UNIT_TEST
#include "test/unit_test.h"
#endif

//-------------------------------------------------------------------------
// commands
//-------------------------------------------------------------------------

static const Command snort_cmds[] =
{
    { "show_plugins", main_dump_plugins, "show available plugins" },
    { "dump_stats", main_dump_stats, "show summary statistics" },
    { "rotate_stats", main_rotate_stats, "roll perfmonitor log files" },
    { "reload_config", main_reload_config, "load new configuration" },
    { "reload_attributes", main_reload_attributes, "load a new hosts.xml" },
    { "process", main_process, "process given pcap" },
    { "pause", main_pause, "suspend packet processing" },
    { "resume", main_resume, "continue packet processing" },
    { "quit", main_quit, "shutdown and dump-stats" },
    { "help", main_help, "this output" },
    { nullptr, nullptr, nullptr }
};

//-------------------------------------------------------------------------
// parameters
//-------------------------------------------------------------------------

static const Parameter s_params[] =
{
    { "-?", Parameter::PT_IMPLIED, nullptr, nullptr,
      "list command line options (same as --help)" },

    { "-A", Parameter::PT_STRING, nullptr, nullptr, 
      "<mode> set alert mode: none, cmg, or alert_*" },

    { "-B", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "<mask> obfuscated IP addresses in alerts and packet dumps using CIDR mask" },

    { "-C", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "print out payloads with character data only (no hex)" },

    { "-c", Parameter::PT_STRING, nullptr, nullptr, 
      "<conf> use this configuration" },

    { "-D", Parameter::PT_IMPLIED, nullptr, nullptr,
      "run Snort in background (daemon) mode" },

    { "-d", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "dump the Application Layer" },

    { "-E", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "enable daemon restart" },

    { "-e", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "display the second layer header info" },

    { "-f", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "turn off fflush() calls after binary log writes" },

    { "-G", Parameter::PT_INT, "0:65535", nullptr, 
      "<0xid> (same as --logid)" },

    { "-g", Parameter::PT_STRING, nullptr, nullptr, 
      "<gname> run snort gid as <gname> group (or gid) after initialization" },

    { "-H", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "make hash tables deterministic" },

    { "-i", Parameter::PT_STRING, nullptr, nullptr, 
      "<iface>... list of interfaces" },

    { "-j", Parameter::PT_PORT, nullptr, nullptr,
      "<port> to listen for telnet connections" },

    { "-K", Parameter::PT_ENUM, "none|text|pcap", "none", 
      "<mode> logging mode" },

    { "-k", Parameter::PT_ENUM, "all|noip|notcp|noudp|noicmp|none", "all", 
      "<mode> checksum mode (all,noip,notcp,noudp,noicmp,none)" },

    { "-l", Parameter::PT_STRING, nullptr, nullptr, 
      "<logdir> log to this directory instead of current director" },

    { "-M", Parameter::PT_IMPLIED, nullptr, nullptr,
      "log messages to syslog (not alerts)" },

    { "-m", Parameter::PT_INT, "0:", nullptr, 
      "<umask> set umask = <umask>" },

    { "-n", Parameter::PT_INT, "0:", nullptr, 
      "<count> stop after count packets" },

    { "-O", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "obfuscate the logged IP addresses" },

    { "-Q", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "enable inline mode operation" },

    { "-q", Parameter::PT_IMPLIED, nullptr, nullptr,
      "quiet mode - Don't show banner and status report" },

    { "-r", Parameter::PT_STRING, nullptr, nullptr, 
      "<pcap>... (same as --pcap-list)" },

    { "-S", Parameter::PT_STRING, nullptr, nullptr, 
      "<n=v> set rules file variable n equal to value v" },

    { "-s", Parameter::PT_INT, "68:65535", nullptr, 
      "<snap> (same as --snaplen)" },

    { "-T", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "test and report on the current Snort configuration" },

    { "-t", Parameter::PT_STRING, nullptr, nullptr, 
      "<dir> chroots process to <dir> after initialization" },

    { "-U", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "use UTC for timestamps" },

    { "-u", Parameter::PT_STRING, nullptr, nullptr, 
      "<uname> run snort as <uname> or <uid> after initialization" },

    { "-V", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "(same as --version)" },

    { "-v", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "be verbose" },

    { "-W", Parameter::PT_IMPLIED, nullptr, nullptr,
      "lists available interfaces" },

#if defined(DLT_IEEE802_11)
    { "-w", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "dump 802.11 management and control frames" },
#endif

    { "-X", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "dump the raw packet data starting at the link layer" },

    { "-x", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "same as --pedantic" },

    { "-y", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "include year in timestamp in the alert and log files" },

    { "-z", Parameter::PT_INT, "1:", nullptr,
      "<count> maximum number of packet threads (same as --max-packet-threads)" },

    { "--alert-before-pass", Parameter::PT_IMPLIED, nullptr, nullptr,
      "process alert, drop, sdrop, or reject before pass; "
       "default is pass before alert, drop,..." },

    { "--bpf", Parameter::PT_STRING, nullptr, nullptr,
      "<filter options> are standard BPF options, as seen in TCPDump" },

    { "--pedantic", Parameter::PT_IMPLIED, nullptr, nullptr, 
      "warnings are fatal" },

    { "--create-pidfile", Parameter::PT_IMPLIED, nullptr, nullptr,
      "create PID file, even when not in Daemon mode" },

    { "--daq", Parameter::PT_STRING, nullptr, nullptr,
      "<type> select packet acquisition module (default is pcap)" },

    { "--daq-dir", Parameter::PT_STRING, nullptr, nullptr,
      "<dir> tell snort where to find desired DAQ" },

    { "--daq-list", Parameter::PT_IMPLIED, nullptr, nullptr,
      "list packet acquisition modules available in optional dir, default is static modules only" },

    { "--daq-mode", Parameter::PT_STRING, nullptr, nullptr,
      "<mode> select the DAQ operating mode" },

    { "--daq-var", Parameter::PT_STRING, nullptr, nullptr,
      "<name=value> specify extra DAQ configuration variable" },

    { "--dump-builtin-rules", Parameter::PT_IMPLIED, nullptr, nullptr,
      "creates stub rule files of all loaded rules libraries" },

    { "--dump-dynamic-rules", Parameter::PT_STRING, nullptr, nullptr,
      "<path> creates stub rule file of all loaded rules libraries" },

    { "--dirty-pig", Parameter::PT_IMPLIED, nullptr, nullptr,
      "don't flush packets and release memory on shutdown" },

    { "--enable-inline-test", Parameter::PT_IMPLIED, nullptr, nullptr,
      "enable Inline-Test Mode Operation" },

    { "--help", Parameter::PT_IMPLIED, nullptr, nullptr,
      "list command line options (same as -?)" },

    { "--help!", Parameter::PT_IMPLIED, nullptr, nullptr,
      "overview of help" },

    { "--help-builtin", Parameter::PT_STRING, "(optional)", nullptr,
      "<module prefix> output matching builtin rules" },

    { "--help-buffers", Parameter::PT_IMPLIED, nullptr, nullptr,
      "output available inspection buffers" },

    { "--help-commands", Parameter::PT_STRING, "(optional)", nullptr,
      "[<module prefix>] output matching commands" },

    { "--help-config", Parameter::PT_STRING, "(optional)", nullptr,
      "[<module prefix>] output matching config options" },

    { "--help-gids", Parameter::PT_STRING, "(optional)", nullptr,
      "[<module prefix>] output matching generators" },

    { "--help-module", Parameter::PT_STRING, nullptr, nullptr,
      "<module> output description of given module" },

    { "--help-modules", Parameter::PT_IMPLIED, nullptr, nullptr,
      "list all available modules with brief help" },

    { "--help-plugins", Parameter::PT_IMPLIED, nullptr, nullptr,
      "list all available plugins with brief help" },

    { "--help-options", Parameter::PT_STRING, "(optional)", nullptr,
      "<option prefix> output matching command line option quick help" },

    { "--help-signals", Parameter::PT_IMPLIED, nullptr, nullptr,
      "dump available control signals" },

    { "--id-subdir", Parameter::PT_IMPLIED, nullptr, nullptr,
      "create/use instance subdirectories in logdir instead of instance filename prefix" },

    { "--id-zero", Parameter::PT_IMPLIED, nullptr, nullptr,
      "use id prefix / subdirectory even with one packet thread" },

    { "--list-modules", Parameter::PT_IMPLIED, nullptr, nullptr,
      "list all known modules" },

    { "--list-plugins", Parameter::PT_IMPLIED, nullptr, nullptr,
      "list all known plugins" },

    { "--lua", Parameter::PT_STRING, nullptr, nullptr,
      "<chunk> extend/override conf with chunk; may be repeated" },

    { "--logid", Parameter::PT_INT, "0:65535", nullptr,
      "<0xid> log Identifier to uniquely id events for multiple snorts (same as -G)" },

    { "--markup", Parameter::PT_IMPLIED, nullptr, nullptr,
      "output help in asciidoc compatible format" },

    { "--max-packet-threads", Parameter::PT_INT, "0:", nullptr,
      "<count> configure maximum number of packet threads (same as -z)" },

    { "--nostamps", Parameter::PT_IMPLIED, nullptr, nullptr,
      "don't include timestamps in log file names" },

    { "--nolock-pidfile", Parameter::PT_IMPLIED, nullptr, nullptr,
      "do not try to lock Snort PID file" },

    { "--pause", Parameter::PT_IMPLIED, nullptr, nullptr,
      "wait for resume/quit command before processing packets/terminating", },

    { "--pcap-file", Parameter::PT_STRING, nullptr, nullptr,
      "<file> file that contains a list of pcaps to read - read mode is implied" },

    { "--pcap-list", Parameter::PT_STRING, nullptr, nullptr,
      "<list> a space separated list of pcaps to read - read mode is implied" },

    { "--pcap-dir", Parameter::PT_STRING, nullptr, nullptr,
      "<dir> a directory to recurse to look for pcaps - read mode is implied" },

    { "--pcap-filter", Parameter::PT_STRING, nullptr, nullptr,
      "<filter> filter to apply when getting pcaps from file or directory" },

    { "--pcap-loop", Parameter::PT_INT, "-1:", nullptr,
      "<count> read all pcaps <count> times;  0 will read until Snort is terminated" },

    { "--pcap-no-filter", Parameter::PT_IMPLIED, nullptr, nullptr,
      "reset to use no filter when getting pcaps from file or directory" },

    { "--pcap-reload", Parameter::PT_IMPLIED, nullptr, nullptr,
      "if reading multiple pcaps, reload snort config between pcaps" },

    { "--pcap-reset", Parameter::PT_IMPLIED, nullptr, nullptr,
      "reset Snort after each pcap" },

    { "--pcap-show", Parameter::PT_IMPLIED, nullptr, nullptr,
      "print a line saying what pcap is currently being read" },

    { "--plugin-path", Parameter::PT_STRING, nullptr, nullptr,
      "<path> where to find plugins" },

    { "--process-all-events", Parameter::PT_IMPLIED, nullptr, nullptr,
      "process all action groups" },

    { "--rule", Parameter::PT_STRING, nullptr, nullptr,
      "<rules> to be added to configuration; may be repeated" },

    { "--run-prefix", Parameter::PT_STRING, nullptr, nullptr,
      "<pfx> prepend this to each output file" },

    { "--script-path", Parameter::PT_STRING, nullptr, nullptr,
      "<path> where to find luajit scripts" },

    { "--shell", Parameter::PT_IMPLIED, nullptr, nullptr,
      "enable the interactive command line", },

    { "--skip", Parameter::PT_INT, "0:", nullptr,
      "<n> skip 1st n packets", },

    { "--snaplen", Parameter::PT_INT, "68:65535", nullptr,
      "<snap> set snaplen of packet (same as -s)", },

    { "--stdin-rules", Parameter::PT_IMPLIED, nullptr, nullptr,
      "read rules from stdin until EOF or a line with EOR is read", },

    { "--treat-drop-as-alert", Parameter::PT_IMPLIED, nullptr, nullptr,
      "converts drop, sdrop, and reject rules into alert rules during startup" },

    { "--treat-drop-as-ignore", Parameter::PT_IMPLIED, nullptr, nullptr,
      "use drop, sdrop, and reject rules to ignore session traffic when not inline" },

#ifdef UNIT_TEST
    { "--unit-test", Parameter::PT_STRING, nullptr, nullptr,
      "<verbosity> run unit tests with given libcheck output mode" },
#endif
    { "--version", Parameter::PT_IMPLIED, nullptr, nullptr,
      "show version number (same as -V)" },

    { nullptr, Parameter::PT_MAX, nullptr, nullptr, nullptr }
};

//-------------------------------------------------------------------------
// module
//-------------------------------------------------------------------------

static const char* s_name = "snort";

static const char* s_help =
    "command line configuration and shell commands";

class SnortModule : public Module
{
public:
    SnortModule() : Module(s_name, s_help, s_params)
    { };

    const Command* get_commands() const
    { return snort_cmds; };

    bool set(const char*, Value&, SnortConfig*);
};

bool SnortModule::set(const char*, Value& v, SnortConfig* sc)
{
    if ( v.is("-?") )
        help_usage(sc, v.get_string());

    else if ( v.is("-A") )
        config_alert_mode(sc, v.get_string());

    else if ( v.is("-B") )
        ConfigObfuscationMask(sc, v.get_string());

    else if ( v.is("-C") )
        ConfigDumpCharsOnly(sc, v.get_string());

    else if ( v.is("-c") )
        config_conf(sc, v.get_string());

    else if ( v.is("-D") )
        config_daemon(sc, v.get_string());

    else if ( v.is("-d") )
        ConfigDumpPayload(sc, v.get_string());

    else if ( v.is("-E") )
    {
        sc->run_flags |= RUN_FLAG__DAEMON_RESTART;
        config_daemon(sc, v.get_string());
    }

    else if ( v.is("-e") )
        ConfigDecodeDataLink(sc, v.get_string());

    else if ( v.is("-f") )
        sc->output_flags |= OUTPUT_FLAG__LINE_BUFFER;

    else if ( v.is("-G") || v.is("--logid") )
        sc->event_log_id = v.get_long() << 16;

    else if ( v.is("-g") )
        ConfigSetGid(sc, v.get_string());

    else if ( v.is("-H") )
        sc->run_flags |= RUN_FLAG__STATIC_HASH;

    else if ( v.is("-i") )
        Trough_Multi(SOURCE_LIST, v.get_string());

    else if ( v.is("-j") )
        sc->remote_control = v.get_long();

    else if ( v.is("-K") )
        config_log_mode(sc, v.get_string());

    else if ( v.is("-k") )
        ConfigChecksumMode(sc, v.get_string());

    else if ( v.is("-l") )
        ConfigLogDir(sc, v.get_string());

    else if ( v.is("-M") )
        config_syslog(sc, v.get_string());

    else if ( v.is("-m") )
        ConfigUmask(sc, v.get_string());

    else if ( v.is("-n") )
        sc->pkt_cnt = v.get_long();

    else if ( v.is("-O") )
        ConfigObfuscate(sc, v.get_string());

    else if ( v.is("-Q") )
        sc->run_flags |= RUN_FLAG__INLINE;

    else if ( v.is("-q") )
        ConfigQuiet(sc, v.get_string());

    else if ( v.is("-r") || v.is("--pcap-list") )
    {
        Trough_Multi(SOURCE_LIST, v.get_string());
        sc->run_flags |= RUN_FLAG__READ;
    }
    else if ( v.is("-S") )
        config_set_var(sc, v.get_string());

    else if ( v.is("-s") )
        sc->pkt_snaplen = v.get_long();

    else if ( v.is("-T") )
        sc->run_flags |= RUN_FLAG__TEST;

    else if ( v.is("-t") )
        ConfigChrootDir(sc, v.get_string());

    else if ( v.is("-U") )
        ConfigUtc(sc, v.get_string());

    else if ( v.is("-u") )
        ConfigSetUid(sc, v.get_string());

    else if ( v.is("-V") )
        help_version(sc, v.get_string());

    else if ( v.is("-v") )
        ConfigVerbose(sc, v.get_string());

    else if ( v.is("-W") )
        list_interfaces(sc, v.get_string());

#if defined(DLT_IEEE802_11)
    else if ( v.is("-w") )
        sc->output_flags |= OUTPUT_FLAG__SHOW_WIFI_MGMT;
#endif

    else if ( v.is("-X") )
        ConfigDumpPayloadVerbose(sc, v.get_string());

    else if ( v.is("-x") || v.is("--pedantic") )
        sc->run_flags |= RUN_FLAG__CONF_ERROR_OUT;

    else if ( v.is("-y") )
        ConfigShowYear(sc, v.get_string());

    else if ( v.is("-z") || v.is("--max-packet-threads") )
        set_instance_max(v.get_long());

    else if ( v.is("--alert-before-pass") )
        ConfigAlertBeforePass(sc, v.get_string());

    else if ( v.is("--bpf") )
        sc->bpf_filter = SnortStrdup(v.get_string());

    else if ( v.is("--create-pidfile") )
        ConfigCreatePidFile(sc, v.get_string());

    else if ( v.is("--daq") )
        ConfigDaqType(sc, v.get_string());

    else if ( v.is("--daq-dir") )
        ConfigDaqDir(sc, v.get_string());

    else if ( v.is("--daq-list") )
        list_daqs(sc, v.get_string());

    else if ( v.is("--daq-mode") )
        ConfigDaqMode(sc, v.get_string());

    else if ( v.is("--daq-var") )
        ConfigDaqVar(sc, v.get_string());

    else if ( v.is("--dump-builtin-rules") )
       dump_builtin_rules(sc, v.get_string());

    else if ( v.is("--dump-dynamic-rules") )
       dump_dynamic_rules(sc, v.get_string());

    else if ( v.is("--dirty-pig") )
        ConfigDirtyPig(sc, v.get_string());

    else if ( v.is("--enable-inline-test") )
        sc->run_flags |= RUN_FLAG__INLINE_TEST;

    else if ( v.is("--help") )
        help_usage(sc, v.get_string());

    else if ( v.is("--help!") )
        help_basic(sc, v.get_string());

    else if ( v.is("--help-builtin") )
        help_builtin(sc, v.get_string());

    else if ( v.is("--help-buffers") )
        help_buffers(sc, v.get_string());

    else if ( v.is("--help-commands") )
        help_commands(sc, v.get_string());

    else if ( v.is("--help-config") )
        help_config(sc, v.get_string());

    else if ( v.is("--help-gids") )
        help_gids(sc, v.get_string());

    else if ( v.is("--help-module") )
        help_module(sc, v.get_string());

    else if ( v.is("--help-modules") )
        help_modules(sc, v.get_string());

    else if ( v.is("--help-plugins") )
        help_plugins(sc, v.get_string());

    else if ( v.is("--help-options") )
        help_options(sc, v.get_string());

    else if ( v.is("--help-signals") )
        help_signals(sc, v.get_string());

    else if ( v.is("--id-subdir") )
        sc->id_subdir = true;

    else if ( v.is("--id-zero") )
        sc->id_zero = true;

    else if ( v.is("--list-modules") )
        list_modules(sc, v.get_string());

    else if ( v.is("--list-plugins") )
        list_plugins(sc, v.get_string());

    else if ( v.is("--lua") )
        sc->policy_map->get_shell()->set_overrides(v.get_string());

    else if ( v.is("--markup") )
        config_markup(sc, v.get_string());

    else if ( v.is("--nostamps") )
        ConfigNoLoggingTimestamps(sc, v.get_string());

    else if ( v.is("--nolock-pidfile") )
        sc->run_flags |= RUN_FLAG__NO_LOCK_PID_FILE;

    else if ( v.is("--pause") )
        sc->run_flags |= RUN_FLAG__PAUSE;

    else if ( v.is("--pcap-file") )
    {
        Trough_Multi(SOURCE_FILE_LIST, v.get_string());
        sc->run_flags |= RUN_FLAG__READ;
    }
    else if ( v.is("--pcap-dir") )
    {
        Trough_Multi(SOURCE_DIR, v.get_string());
        sc->run_flags |= RUN_FLAG__READ;
    }
    else if ( v.is("--pcap-filter") )
        Trough_SetFilter(v.get_string());

    else if ( v.is("--pcap-loop") )
        Trough_SetLoopCount(v.get_long());

    else if ( v.is("--pcap-no-filter") )
        Trough_SetFilter(NULL);

    else if ( v.is("--pcap-reload") )
        sc->run_flags |= RUN_FLAG__PCAP_RELOAD;

    else if ( v.is("--pcap-reset") )
        sc->run_flags |= RUN_FLAG__PCAP_RESET;

    else if ( v.is("--pcap-show") )
        sc->run_flags |= RUN_FLAG__PCAP_SHOW;

    else if ( v.is("--plugin-path") )
        ConfigPluginPath(sc, v.get_string());

    else if ( v.is("--process-all-events") )
        ConfigProcessAllEvents(sc, v.get_string());

    else if ( v.is("--rule") )
        parser_append_rules(v.get_string());

    else if ( v.is("--run-prefix") )
        sc->run_prefix = SnortStrdup(v.get_string());

    else if ( v.is("--script-path") )
        ConfigScriptPath(sc, v.get_string());

    else if ( v.is("--shell") )
        sc->run_flags |= RUN_FLAG__SHELL;

    else if ( v.is("--skip") )
        sc->pkt_skip = v.get_long();

    else if ( v.is("--snaplen") )
        sc->pkt_snaplen = v.get_long();

    else if ( v.is("--stdin-rules") )
        sc->stdin_rules = true;

    else if ( v.is("--treat-drop-as-alert") )
        ConfigTreatDropAsAlert(sc, v.get_string());

    else if ( v.is("--treat-drop-as-ignore") )
        ConfigTreatDropAsIgnore(sc, v.get_string());

#ifdef UNIT_TEST
    else if ( v.is("--unit-test") )
        unit_test_mode(v.get_string());
#endif
    else if ( v.is("--version") )
        help_version(sc, v.get_string());

    else
        return false;

    return true;
}

//-------------------------------------------------------------------------
// singleton
//-------------------------------------------------------------------------

static SnortModule* snort_module = nullptr;

Module* get_snort_module()
{
    if ( !snort_module )
        snort_module = new SnortModule;

    return snort_module;
}


