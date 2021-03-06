/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "focuschain.h"
#include "abstract_client.h"
#include "workspace.h"

namespace KWin
{

KWIN_SINGLETON_FACTORY_VARIABLE(FocusChain, s_manager)

FocusChain::FocusChain(QObject *parent)
    : QObject(parent)
    , m_separateScreenFocus(false)
    , m_activeClient(nullptr)
{
}

FocusChain::~FocusChain()
{
    s_manager = nullptr;
}

void FocusChain::remove(AbstractClient *client)
{
    for (auto it = m_desktopFocusChains.begin();
            it != m_desktopFocusChains.end();
            ++it) {
        it.value().removeAll(client);
    }
    m_mostRecentlyUsed.removeAll(client);
}

void FocusChain::addDesktop(VirtualDesktop *desktop)
{
    m_desktopFocusChains.insert(desktop, Chain());
}

void FocusChain::removeDesktop(VirtualDesktop *desktop)
{
    if (m_currentDesktop == desktop) {
        m_currentDesktop = nullptr;
    }
    m_desktopFocusChains.remove(desktop);
}

AbstractClient *FocusChain::getForActivation(VirtualDesktop *desktop) const
{
    return getForActivation(desktop, workspace()->activeOutput());
}

AbstractClient *FocusChain::getForActivation(VirtualDesktop *desktop, AbstractOutput *output) const
{
    auto it = m_desktopFocusChains.constFind(desktop);
    if (it == m_desktopFocusChains.constEnd()) {
        return nullptr;
    }
    const auto &chain = it.value();
    for (int i = chain.size() - 1; i >= 0; --i) {
        auto tmp = chain.at(i);
        // TODO: move the check into Client
        if (!tmp->isShade() && tmp->isShown() && tmp->isOnCurrentActivity()
            && ( !m_separateScreenFocus || tmp->output() == output)) {
            return tmp;
        }
    }
    return nullptr;
}

void FocusChain::update(AbstractClient *client, FocusChain::Change change)
{
    if (!client->wantsTabFocus()) {
        // Doesn't want tab focus, remove
        remove(client);
        return;
    }

    if (client->isOnAllDesktops()) {
        // Now on all desktops, add it to focus chains it is not already in
        for (auto it = m_desktopFocusChains.begin();
                it != m_desktopFocusChains.end();
                ++it) {
            auto &chain = it.value();
            // Making first/last works only on current desktop, don't affect all desktops
            if (it.key() == m_currentDesktop
                    && (change == MakeFirst || change == MakeLast)) {
                if (change == MakeFirst) {
                    makeFirstInChain(client, chain);
                } else {
                    makeLastInChain(client, chain);
                }
            } else {
                insertClientIntoChain(client, chain);
            }
        }
    } else {
        // Now only on desktop, remove it anywhere else
        for (auto it = m_desktopFocusChains.begin();
                it != m_desktopFocusChains.end();
                ++it) {
            auto &chain = it.value();
            if (client->isOnDesktop(it.key())) {
                updateClientInChain(client, change, chain);
            } else {
                chain.removeAll(client);
            }
        }
    }

    // add for most recently used chain
    updateClientInChain(client, change, m_mostRecentlyUsed);
}

void FocusChain::updateClientInChain(AbstractClient *client, FocusChain::Change change, Chain &chain)
{
    if (change == MakeFirst) {
        makeFirstInChain(client, chain);
    } else if (change == MakeLast) {
        makeLastInChain(client, chain);
    } else {
        insertClientIntoChain(client, chain);
    }
}

void FocusChain::insertClientIntoChain(AbstractClient *client, Chain &chain)
{
    if (chain.contains(client)) {
        return;
    }
    if (m_activeClient && m_activeClient != client &&
            !chain.empty() && chain.last() == m_activeClient) {
        // Add it after the active client
        chain.insert(chain.size() - 1, client);
    } else {
        // Otherwise add as the first one
        chain.append(client);
    }
}

void FocusChain::moveAfterClient(AbstractClient *client, AbstractClient *reference)
{
    if (!client->wantsTabFocus()) {
        return;
    }

    for (auto it = m_desktopFocusChains.begin();
            it != m_desktopFocusChains.end();
            ++it) {
        if (!client->isOnDesktop(it.key())) {
            continue;
        }
        moveAfterClientInChain(client, reference, it.value());
    }
    moveAfterClientInChain(client, reference, m_mostRecentlyUsed);
}

void FocusChain::moveAfterClientInChain(AbstractClient *client, AbstractClient *reference, Chain &chain)
{
    if (!chain.contains(reference)) {
        return;
    }
    if (AbstractClient::belongToSameApplication(reference, client)) {
        chain.removeAll(client);
        chain.insert(chain.indexOf(reference), client);
    } else {
        chain.removeAll(client);
        for (int i = chain.size() - 1; i >= 0; --i) {
            if (AbstractClient::belongToSameApplication(reference, chain.at(i))) {
                chain.insert(i, client);
                break;
            }
        }
    }
}

AbstractClient *FocusChain::firstMostRecentlyUsed() const
{
    if (m_mostRecentlyUsed.isEmpty()) {
        return nullptr;
    }
    return m_mostRecentlyUsed.first();
}

AbstractClient *FocusChain::nextMostRecentlyUsed(AbstractClient *reference) const
{
    if (m_mostRecentlyUsed.isEmpty()) {
        return nullptr;
    }
    const int index = m_mostRecentlyUsed.indexOf(reference);
    if (index == -1) {
        return m_mostRecentlyUsed.first();
    }
    if (index == 0) {
        return m_mostRecentlyUsed.last();
    }
    return m_mostRecentlyUsed.at(index - 1);
}

// copied from activation.cpp
bool FocusChain::isUsableFocusCandidate(AbstractClient *c, AbstractClient *prev) const
{
    return c != prev &&
           !c->isShade() && c->isShown() && c->isOnCurrentDesktop() && c->isOnCurrentActivity() &&
           (!m_separateScreenFocus || c->isOnOutput(prev ? prev->output() : workspace()->activeOutput()));
}

AbstractClient *FocusChain::nextForDesktop(AbstractClient *reference, VirtualDesktop *desktop) const
{
    auto it = m_desktopFocusChains.constFind(desktop);
    if (it == m_desktopFocusChains.constEnd()) {
        return nullptr;
    }
    const auto &chain = it.value();
    for (int i = chain.size() - 1; i >= 0; --i) {
        auto client = chain.at(i);
        if (isUsableFocusCandidate(client, reference)) {
            return client;
        }
    }
    return nullptr;
}

void FocusChain::makeFirstInChain(AbstractClient *client, Chain &chain)
{
    chain.removeAll(client);
    if (options->moveMinimizedWindowsToEndOfTabBoxFocusChain()) {
        if (client->isMinimized()) { // add it before the first minimized ...
            for (int i = chain.count()-1; i >= 0; --i) {
                if (chain.at(i)->isMinimized()) {
                    chain.insert(i+1, client);
                    return;
                }
            }
            chain.prepend(client); // ... or at end of chain
        } else {
            chain.append(client);
        }
    } else {
        chain.append(client);
    }
}

void FocusChain::makeLastInChain(AbstractClient *client, Chain &chain)
{
    chain.removeAll(client);
    chain.prepend(client);
}

bool FocusChain::contains(AbstractClient *client, VirtualDesktop *desktop) const
{
    auto it = m_desktopFocusChains.constFind(desktop);
    if (it == m_desktopFocusChains.constEnd()) {
        return false;
    }
    return it.value().contains(client);
}

} // namespace
