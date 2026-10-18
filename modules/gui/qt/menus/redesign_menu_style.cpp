// SPDX-License-Identifier: GPL-2.0-or-later
#include "redesign_menu_style.hpp"
#include "maininterface/mainctx.hpp"
#include "util/color_scheme_model.hpp"

#include <QApplication>
#include <QMenu>
#include <QStyleHints>
#include <QPainterPath>
#include <QRegion>
#include <QSet>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>
#include <QPainter>
#include <QEvent>

namespace {
class RedesignMenuMask final : public QObject
{
public:
    explicit RedesignMenuMask(QMenu* menu) : QObject(menu) { menu->installEventFilter(this); }
protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Resize) {
            auto* menu = static_cast<QMenu*>(object);
            QPainterPath outline;
            const auto radius = menu->property("redesignMenuRadius").toReal();
            outline.addRoundedRect(menu->rect(), radius, radius);
            menu->setMask(QRegion(outline.toFillPolygon().toPolygon()));
        }
        return false;
    }
};

class RedesignMenuStyle final : public QProxyStyle
{
public:
    explicit RedesignMenuStyle(QObject* owner) : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
    {
        setParent(owner);
        setObjectName(QStringLiteral("redesignMenuStyle"));
    }
    void configure(MainCtx* ctx)
    {
        const auto scheme = ctx->getColorScheme()->currentScheme();
        const bool light = scheme == ColorSchemeModel::Day ||
            (scheme == ColorSchemeModel::System && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Light);
        m_scale = ctx->getIntfScaleFactor();
        m_background = QColor(light ? "#acacac" : "#191919");
        m_background.setAlpha(242);
        m_foreground = QColor(light ? "#404040" : "#8f8f8f");
        m_disabled = QColor(light ? "#737373" : "#595959");
        m_hover = QColor(light ? "#d5d5d5" : "#292929");
        m_border = QColor(light ? "#969696" : "#222222");
        m_accent = ctx->redesignAccent();
    }
    int dp(int value) const { return qMax(1, qRound(value * m_scale)); }
    QPalette menuPalette(QPalette palette) const
    {
        palette.setColor(QPalette::Window, m_background);
        palette.setColor(QPalette::Base, m_background);
        palette.setColor(QPalette::Text, m_foreground);
        palette.setColor(QPalette::WindowText, m_foreground);
        palette.setColor(QPalette::ButtonText, m_foreground);
        palette.setColor(QPalette::Highlight, m_hover);
        palette.setColor(QPalette::HighlightedText, m_foreground);
        palette.setColor(QPalette::Disabled, QPalette::Text, m_disabled);
        return palette;
    }
    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& size, const QWidget* widget) const override
    {
        if (type == CT_MenuItem) {
            const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
            if (item) {
                if (item->menuItemType == QStyleOptionMenuItem::Separator) return QSize(1, dp(1));
                if (item->menuItemType == QStyleOptionMenuItem::Normal || item->menuItemType == QStyleOptionMenuItem::DefaultItem || item->menuItemType == QStyleOptionMenuItem::SubMenu) {
                    const auto label = item->text.section(QLatin1Char('\t'), 0, 0);
                    const auto hint = item->text.section(QLatin1Char('\t'), 1);
                    // QMenu adds the shared shortcut column after it measures each item.
                    const int trailing = item->menuItemType == QStyleOptionMenuItem::SubMenu ? dp(44) : hint.isEmpty() ? dp(12) : dp(28);
                    return QSize(dp(49) + item->fontMetrics.horizontalAdvance(label) + trailing, dp(48));
                }
            }
        }
        return QProxyStyle::sizeFromContents(type, option, size, widget);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override
    {
        switch (metric) {
        case PM_MenuPanelWidth: return 0;
        case PM_MenuHMargin: case PM_MenuVMargin: return dp(4);
        case PM_SmallIconSize: return dp(24);
        case PM_MenuScrollerHeight: return dp(24);
        default: return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr, QStyleHintReturn* data = nullptr) const override
    {
        if (hint == SH_Menu_Scrollable) return true;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override
    {
        if (element == PE_PanelMenu) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setCompositionMode(QPainter::CompositionMode_Source);
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_background);
            painter->drawRoundedRect(option->rect, dp(10), dp(10));
            painter->restore();
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override
    {
        const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (element != CE_MenuItem || !item ||
            (item->menuItemType != QStyleOptionMenuItem::Normal && item->menuItemType != QStyleOptionMenuItem::DefaultItem &&
             item->menuItemType != QStyleOptionMenuItem::SubMenu && item->menuItemType != QStyleOptionMenuItem::Separator)) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }
        if (item->menuItemType == QStyleOptionMenuItem::Separator) {
            painter->fillRect(item->rect, m_border);
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setLayoutDirection(item->direction);
        QRect rect = item->rect;
        if (widget) rect.setRight(qMin(rect.right(), widget->width() - dp(4) - 1));
        if (item->state & State_Selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_hover);
            painter->drawRoundedRect(rect, dp(6), dp(6));
        }
        const bool enabled = item->state & State_Enabled;
        const QColor foreground = enabled ? m_foreground : m_disabled;
        const auto visual = [&](const QRect& box) { return visualRect(item->direction, rect, box); };
        const QRect icon = visual(QRect(rect.x() + dp(14), rect.center().y() - dp(12), dp(24), dp(24)));
        if (!item->icon.isNull()) {
            const auto state = item->checked ? QIcon::On : QIcon::Off;
            if (enabled) item->icon.paint(painter, icon, Qt::AlignCenter, QIcon::Normal, state);
            else {
                // Use the text's disabled colour instead of Qt's generated icon tint.
                auto pixmap = item->icon.pixmap(icon.size(), painter->device()->devicePixelRatioF(), QIcon::Normal, state);
                QPainter tint(&pixmap);
                tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tint.fillRect(pixmap.rect(), m_disabled);
                tint.end();
                drawItemPixmap(painter, icon, Qt::AlignCenter, pixmap);
            }
        }
        else if (item->checked) {
            painter->setPen(QPen(enabled ? m_accent : m_disabled, 1.5 * m_scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            QPainterPath check;
            check.moveTo(icon.x() + 6 * m_scale, icon.y() + 12 * m_scale);
            check.lineTo(icon.x() + 10 * m_scale, icon.y() + 16 * m_scale);
            check.lineTo(icon.x() + 18 * m_scale, icon.y() + 8 * m_scale);
            painter->drawPath(check);
        }
        const auto label = item->text.section(QLatin1Char('\t'), 0, 0);
        const auto hint = item->text.section(QLatin1Char('\t'), 1);
        int trailing = dp(12);
        painter->setFont(item->font);
        if (item->menuItemType == QStyleOptionMenuItem::SubMenu) {
            const QRect arrow = visual(QRect(rect.right() - dp(37) + 1, rect.center().y() - dp(12), dp(24), dp(24)));
            painter->setPen(QPen(foreground, 1.5 * m_scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath chevron;
            const bool rtl = item->direction == Qt::RightToLeft;
            chevron.moveTo(arrow.x() + (rtl ? 14 : 10) * m_scale, arrow.y() + 7 * m_scale);
            chevron.lineTo(arrow.x() + (rtl ? 9 : 15) * m_scale, arrow.y() + 12 * m_scale);
            chevron.lineTo(arrow.x() + (rtl ? 14 : 10) * m_scale, arrow.y() + 17 * m_scale);
            painter->drawPath(chevron);
            trailing = dp(44);
        } else if (!hint.isEmpty()) {
            const int width = item->fontMetrics.horizontalAdvance(hint);
            QColor hintColor = foreground;
            hintColor.setAlpha(166);
            painter->setPen(hintColor);
            painter->drawText(visual(QRect(rect.right() - dp(12) - width + 1, rect.y(), width, rect.height())), Qt::AlignVCenter | Qt::AlignLeading, hint);
            trailing += width + dp(16);
        }
        painter->setPen(foreground);
        const QRect text = visual(rect.adjusted(dp(49), 0, -trailing, 0));
        const int flags = Qt::AlignVCenter | Qt::AlignLeading |
            (styleHint(SH_UnderlineShortcut, item, widget) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic);
        painter->drawText(text, flags, item->fontMetrics.elidedText(label, Qt::ElideRight, text.width()));
        painter->restore();
    }
private:
    qreal m_scale = 1;
    QColor m_background, m_foreground, m_disabled, m_hover, m_border, m_accent;
};

}

void styleRedesignMenus(QMenu* root, MainCtx* ctx)
{
    const auto dp = [scale = ctx->getIntfScaleFactor()](int value) { return qMax(1, qRound(value * scale)); };
    QFont font = qApp->font();
    font.setPixelSize(dp(14));
    QSet<QMenu*> visited;
    const auto apply = [&](auto&& self, QMenu* menu) -> void {
        if (!menu || visited.contains(menu)) return;
        visited.insert(menu);
        auto* style = static_cast<RedesignMenuStyle*>(menu->findChild<QProxyStyle*>(QStringLiteral("redesignMenuStyle"), Qt::FindDirectChildrenOnly));
        if (!style) style = new RedesignMenuStyle(menu);
        style->configure(ctx);
        menu->setStyle(style);
        menu->setPalette(style->menuPalette(menu->palette()));
        menu->setFont(font);
        menu->setMinimumWidth(dp(292));
        menu->setWindowFlag(Qt::NoDropShadowWindowHint);
        menu->setAttribute(Qt::WA_TranslucentBackground);
        menu->setProperty("redesignMenuRadius", dp(10));
        if (!menu->property("redesignMenuMaskInstalled").toBool()) {
            new RedesignMenuMask(menu);
            menu->setProperty("redesignMenuMaskInstalled", true);
        }
        for (auto* action : menu->actions()) self(self, action->menu());
    };
    apply(apply, root);
}

