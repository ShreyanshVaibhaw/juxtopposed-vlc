// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_REDESIGN_MENU_STYLE_HPP
#define VLC_QT_REDESIGN_MENU_STYLE_HPP

class QMenu;
class MainCtx;

// Apply the player menu appearance to the menu and its existing submenus.
void styleRedesignMenus(QMenu* menu, MainCtx* ctx);

// Keep existing menus in sync and include menus created by Qt and dialogs.
void watchRedesignMenus(MainCtx* ctx);

#endif
