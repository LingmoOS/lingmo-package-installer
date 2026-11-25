// SPDX-FileCopyrightText: 2026 LingmoOS Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "globalmenu.h"

#include "base/event_loop.h"

namespace PackageInstaller {
using namespace Lingmo::base::dbus;
namespace {
constexpr uint32_t kApplicationWindowId = 1;
constexpr const char* kInterface = "com.canonical.dbusmenu";
constexpr const char* kRegistrar = "com.canonical.AppMenu.Registrar";
constexpr const char* kRegistrarPath = "/com/canonical/AppMenu/Registrar";
}

GlobalMenu::GlobalMenu(Connection* bus, std::function<std::vector<Entry>()> entries)
    : m_bus(bus), m_entries(std::move(entries)), m_object(bus, kPath, kInterface)
{
    rebuild();
    if (!bus)
        return;
    m_object.addProperty("Version", [] { return uint32_t(3); });
    m_object.addProperty("TextDirection", [] { return std::string("ltr"); });
    m_object.addProperty("Status", [] { return std::string("normal"); });
    m_object.addProperty("IconThemePath", [] { return std::vector<std::string>(); });
    m_object.addMethod("GetLayout", "iias", "u(ia{sv}av)", [this](MethodCall& call) {
        int32_t parent = 0;
        int32_t depth = -1;
        std::vector<std::string> names;
        call.read(parent, depth, names);
        if (parent && !find(parent)) {
            call.replyError("com.canonical.dbusmenu.Error", "No such item");
            return;
        }
        const Entry* entry = find(parent);
        call.reply(uint32_t(1), layout(parent, entry ? entry->children : m_items, depth));
    });
    m_object.addMethod("GetGroupProperties", "aias", "a(ia{sv})", [this](MethodCall& call) {
        std::vector<int32_t> ids;
        std::vector<std::string> names;
        call.read(ids, names);
        std::vector<std::tuple<int32_t, VariantMap>> result;
        for (int32_t id : ids)
            if (const Entry* entry = find(id))
                result.emplace_back(id, properties(*entry));
        call.reply(result);
    });
    m_object.addMethod("GetProperty", "is", "v", [this](MethodCall& call) {
        int32_t id = 0;
        std::string name;
        call.read(id, name);
        const Entry* entry = find(id);
        if (!entry) {
            call.replyError("com.canonical.dbusmenu.Error", "No such item");
            return;
        }
        const auto values = properties(*entry);
        const auto value = values.find(name);
        call.reply(value == values.end() ? Variant(std::string()) : value->second);
    });
    m_object.addMethod("Event", "isvu", "", [this](MethodCall& call) {
        int32_t id = 0;
        std::string event;
        call.read(id, event);
        call.reply();
        const Entry* entry = find(id);
        if (event == "clicked" && entry && entry->action)
            Lingmo::base::EventLoop::current()->post(
                [action = entry->action, alive = m_alive] { if (*alive) action(); });
    });
    m_object.addMethod("AboutToShow", "i", "b", [](MethodCall& call) {
        int32_t id = 0;
        call.read(id);
        call.reply(id == 0);
    });
    if (!m_object.publish())
        return;
    m_registrar = bus->watchName(kRegistrar, [this, alive = m_alive](const std::string& owner) {
        if (!*alive)
            return;
        m_registration = {};
        setAvailable(false);
        if (!owner.empty())
            registerWindow();
    });
}

GlobalMenu::~GlobalMenu()
{
    *m_alive = false;
    if (m_bus) {
        Proxy registrar(m_bus, kRegistrar, kRegistrarPath, kRegistrar);
        registrar.send("UnregisterWindow", kApplicationWindowId);
    }
}

void GlobalMenu::registerWindow()
{
    Proxy registrar(m_bus, kRegistrar, kRegistrarPath, kRegistrar);
    m_registration = registrar.callAsync("RegisterWindow", [this, alive = m_alive](const Reply& reply) {
        if (*alive)
            setAvailable(!reply.isError());
    }, kApplicationWindowId, ObjectPath{kPath});
}

void GlobalMenu::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    availabilityChanged.emit(available);
}

void GlobalMenu::rebuild()
{
    m_items = m_entries();
    m_ids.clear();
    m_itemIds.clear();
    int32_t next = 1;
    std::function<void(const std::vector<Entry>&)> number = [&](const auto& entries) {
        for (const Entry& entry : entries) {
            m_ids[next] = &entry;
            m_itemIds[&entry] = next++;
            number(entry.children);
        }
    };
    number(m_items);
}

const GlobalMenu::Entry* GlobalMenu::find(int32_t id) const
{
    const auto it = m_ids.find(id);
    return it == m_ids.end() ? nullptr : it->second;
}

VariantMap GlobalMenu::properties(const Entry& entry) const
{
    VariantMap result;
    result["label"] = Variant(entry.label);
    result["enabled"] = Variant(true);
    result["visible"] = Variant(true);
    if (!entry.children.empty())
        result["children-display"] = Variant(std::string("submenu"));
    if (!entry.shortcut.empty())
        result["shortcut"] = Variant(std::vector<std::vector<std::string>>{{entry.shortcut}});
    return result;
}

GlobalMenu::Layout GlobalMenu::layout(int32_t id, const std::vector<Entry>& children, int depth) const
{
    VariantMap props;
    if (!id)
        props["children-display"] = Variant(std::string("submenu"));
    else if (const Entry* entry = find(id))
        props = properties(*entry);
    std::vector<Variant> items;
    if (depth != 0) {
        for (const Entry& child : children) {
            const auto found = m_itemIds.find(&child);
            if (found != m_itemIds.end())
                items.emplace_back(layout(found->second, child.children, depth < 0 ? -1 : depth - 1));
        }
    }
    return {id, std::move(props), std::move(items)};
}

} // namespace PackageInstaller
