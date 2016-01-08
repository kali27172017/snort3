//--------------------------------------------------------------------------
// Copyright (C) 2015-2015 Cisco and/or its affiliates. All rights reserved.
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

// profiler_printer.h author Joel Cornett <jocornet@cisco.com>

#ifndef PROFILER_PRINTER_H
#define PROFILER_PRINTER_H

#include <cassert>
#include <algorithm>
#include <functional>
#include <sstream>
#include <string>

#include "log/messages.h"

#include "profiler_stats_table.h"
#include "profiler_tree_builder.h"

template<typename View>
struct ProfilerSorter
{
    using Entry = typename ProfilerBuilder<View>::Entry;
    using SortFn = bool(*)(const View& rhs, const View& lhs);

    std::string name;
    SortFn sort;

    operator bool() const
    { return sort != nullptr; }

    bool operator()(const Entry& lhs, const Entry& rhs) const
    { return (*this)(lhs.view, rhs.view); }

    bool operator()(const View& lhs, const View& rhs) const
    {
        assert(sort);
        return sort(lhs, rhs);
    }
};

template<typename View>
class ProfilerPrinter
{
public:
    using Entry = typename ProfilerBuilder<View>::Entry;
    using Sorter = ProfilerSorter<View>;
    using PrintFn = std::function<void(StatsTable&, const View&)>;

    ProfilerPrinter(const StatsTable::Field* fields, const PrintFn print, const Sorter& sort) :
        fields(fields), print(print), sort(sort) { }

    void print_table(std::string title, Entry& root, unsigned count)
    {
        std::ostringstream ss;

        {
            StatsTable table(fields, ss);

            table << StatsTable::SEP;
            table << title;

            if ( count )
                table << " (worst " << count;
            else
                table << " (all";

            if ( sort )
                table << ", sorted by " << sort.name;

            table << ")\n";

            table << StatsTable::HEADER;
        }

        LogMessage("%s", ss.str().c_str());

        print_children(root, root, 0, count);
        print_row(root, root, 0, 0);
    }

    void print_children(const Entry& root, Entry& cur, int layer, unsigned count)
    {
        auto& entries = cur.children;

        if ( !count || count > entries.size() )
            count = entries.size();

        if ( sort )
            std::partial_sort(entries.begin(), entries.begin() + count, entries.end(), sort);

        for ( unsigned i = 0; i < count; ++i )
        {
            auto& entry = entries[i];
            print_row(root, entry, layer + 1, i + 1);
            print_children(root, entry, layer + 1, count);
        }
    }

    void print_row(const Entry& root, const Entry& cur, int layer, unsigned num)
    {
        std::ostringstream ss;

        {
            StatsTable table(fields, ss);

            table << StatsTable::ROW;

            // if we're printing the root node
            if ( root == cur )
                table << "--" << root.view.name << "--";

            else
            {
                auto indent = std::string(layer, ' ') + std::to_string(num);
                table << indent << cur.view.name << layer;
            }

            // delegate to user function
            print(table, cur.view);

            // don't need to print %/caller or %/total if root
            if ( root == cur )
                table << "--" << "--";

            else
                table << cur.view.pct_caller() << cur.view.pct_of(root.view.get_stats());
        }

        LogMessage("%s", ss.str().c_str());
    }

private:
    const StatsTable::Field* fields;
    const PrintFn print;
    const Sorter& sort;
};

#endif