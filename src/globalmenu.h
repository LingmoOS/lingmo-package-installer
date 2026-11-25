// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "base/dbus.h"
#include "base/signal.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace PackageInstaller {

class GlobalMenu
{
public:
    struct Entry
    {
        std::string label;
        std::function<void()> action;
        std::vector<Entry> children;
        std::string shortcut;
    };

    static constexpr const char* kPath = "/org/lingmo/PackageInstaller/Menu";
    GlobalMenu(Lingmo::base::dbus::Connection* bus, std::function<std::vector<Entry>()> entries);
    ~GlobalMenu();

    bool isAvailable() const { return m_available; }
    Lingmo::base::Signal<bool> availabilityChanged;

private:
    using Layout = std::tuple<int32_t, Lingmo::base::dbus::VariantMap,
                              std::vector<Lingmo::base::dbus::Variant>>;
    void registerWindow();
    void setAvailable(bool available);
    void rebuild();
    const Entry* find(int32_t id) const;
    Lingmo::base::dbus::VariantMap properties(const Entry& entry) const;
    Layout layout(int32_t id, const std::vector<Entry>& children, int depth) const;

    Lingmo::base::dbus::Connection* m_bus;
    std::function<std::vector<Entry>()> m_entries;
    Lingmo::base::dbus::Object m_object;
    std::vector<Entry> m_items;
    std::map<int32_t, const Entry*> m_ids;
    std::map<const Entry*, int32_t> m_itemIds;
    Lingmo::base::dbus::NameWatch m_registrar;
    Lingmo::base::dbus::PendingCall m_registration;
    bool m_available = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace PackageInstaller
