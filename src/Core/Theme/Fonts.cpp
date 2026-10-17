#include "Fonts.hpp"

#include <QApplication>
#include <QFontDatabase>

namespace Acheron {
namespace Core {
namespace Theme {

namespace {
bool plexSansLoaded = false;
bool plexMonoLoaded = false;

// Qord's type: IBM Plex Sans a little larger than the system's UI size
// (about 14 px for the interface and 15 px for messages at 96 dpi), Plex
// Mono for code. Without the bundled files, the system's fonts.
QFont scaled(const QFont &base, const QString &family, bool loaded, qreal factor)
{
    QFont font = base;
    if (loaded)
        font.setFamily(family);
    if (base.pointSizeF() > 0)
        font.setPointSizeF(qRound(base.pointSizeF() * factor * 2) / 2.0);
    return font;
}
} // namespace

void registerBundledFonts()
{
    static const char *const files[] = {
        "IBMPlexSans-Regular", "IBMPlexSans-Italic",   "IBMPlexSans-Medium",   "IBMPlexSans-MediumItalic",
        "IBMPlexSans-SemiBold", "IBMPlexSans-SemiBoldItalic", "IBMPlexSans-Bold", "IBMPlexSans-BoldItalic",
        "IBMPlexMono-Regular", "IBMPlexMono-Italic",   "IBMPlexMono-Bold",
    };
    for (const char *file : files) {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/resources/fonts/plex/%1.ttf")
                                                                 .arg(QLatin1String(file)));
        if (id == -1)
            continue;
        if (QLatin1String(file).startsWith(QLatin1String("IBMPlexSans")))
            plexSansLoaded = true;
        else
            plexMonoLoaded = true;
    }
}

const std::vector<FontDescriptor> &fontRegistry()
{
    static const std::vector<FontDescriptor> reg = {
        { FontRole::Ui, "font.ui", "Interface",
          scaled(QApplication::font(), QStringLiteral("IBM Plex Sans"), plexSansLoaded, 1.15) },
        { FontRole::Message, "font.message", "Messages",
          scaled(QApplication::font(), QStringLiteral("IBM Plex Sans"), plexSansLoaded, 1.25) },
        { FontRole::Code, "font.code", "Code",
          scaled(QFontDatabase::systemFont(QFontDatabase::FixedFont), QStringLiteral("IBM Plex Mono"), plexMonoLoaded,
                 1.1) },
    };
    return reg;
}

const FontDescriptor *findFontById(const QString &id)
{
    for (const FontDescriptor &d : fontRegistry()) {
        if (id == QLatin1String(d.id))
            return &d;
    }
    return nullptr;
}

const FontDescriptor &fontDescriptor(FontRole role)
{
    for (const FontDescriptor &d : fontRegistry()) {
        if (d.role == role)
            return d;
    }
    return fontRegistry().front(); // just in case
}

} // namespace Theme
} // namespace Core
} // namespace Acheron
