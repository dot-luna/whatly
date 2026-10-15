#include "embeddedsettingsview.h"
#include "embeddedsettingsbridge.h"

#include <QColor>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QWebChannel>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>

namespace {
bool bundledResource(const QUrl &url) {
  return url.scheme() == QLatin1String("qrc") &&
         (url.path().startsWith(QLatin1String("/whatly/settings/")) ||
          url.path() == QLatin1String("/qtwebchannel/qwebchannel.js"));
}

class LocalResourcesOnly : public QWebEngineUrlRequestInterceptor {
public:
  using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
  void interceptRequest(QWebEngineUrlRequestInfo &request) override {
    request.block(!bundledResource(request.requestUrl()));
  }
};

class LocalSettingsPage : public QWebEnginePage {
public:
  LocalSettingsPage(QWebEngineProfile *profile, QObject *parent)
      : QWebEnginePage(profile, parent) {}
protected:
  bool acceptNavigationRequest(const QUrl &url, NavigationType, bool) override {
    return url == QUrl(QStringLiteral("qrc:/whatly/settings/settings.html"));
  }
  QWebEnginePage *createWindow(WebWindowType) override { return nullptr; }
};
} // namespace

EmbeddedSettingsView::EmbeddedSettingsView(SettingsWidget *settings,
                                           QWidget *parent)
    : QWebEngineView(parent) {
  setObjectName(QStringLiteral("whatlyEmbeddedSettings"));
  setContextMenuPolicy(Qt::NoContextMenu);
  m_profile = new QWebEngineProfile;
  m_profile->setUrlRequestInterceptor(new LocalResourcesOnly(m_profile));
  m_localPage = new LocalSettingsPage(m_profile, this);
  m_localPage->settings()->setAttribute(
      QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
  m_localPage->settings()->setAttribute(
      QWebEngineSettings::LocalContentCanAccessFileUrls, false);
  setPage(m_localPage);

  auto *channel = new QWebChannel(m_localPage);
  m_bridge = new EmbeddedSettingsBridge(settings, channel);
  channel->registerObject(QStringLiteral("settingsBridge"), m_bridge);
  m_localPage->setWebChannel(channel);
  connect(m_bridge, &EmbeddedSettingsBridge::closeRequested,
          this, &EmbeddedSettingsView::closeRequested);
  connect(this, &QWebEngineView::loadFinished, this, [this](bool ok) {
    if (ok) {
      setAppearance(m_appearance);
      refreshSettings();
    }
  });
  load(QUrl(QStringLiteral("qrc:/whatly/settings/settings.html")));
}

EmbeddedSettingsView::~EmbeddedSettingsView() {
  // Chromium requires the page to be destroyed before its private profile.
  delete m_localPage;
  delete m_profile;
}

void EmbeddedSettingsView::setAppearance(const QVariantMap &appearance) {
  QVariantMap valid;
  for (const auto *key : {"background", "foreground", "muted", "accent"}) {
    const QString value = appearance.value(QLatin1String(key)).toString();
    // Computed browser colors use rgb()/rgba(), which QColor's named-color
    // parser does not accept. The frontend also checks CSS.supports before use.
    static const QRegularExpression cssRgb(QStringLiteral("^rgba?\\([0-9.,% /]+\\)$"));
    if (value.size() <= 128 &&
        (QColor::isValidColorName(value) || cssRgb.match(value).hasMatch()))
      valid.insert(QLatin1String(key), value);
  }
  const QString mode = appearance.value(QStringLiteral("mode")).toString();
  if (mode == QLatin1String("dark") || mode == QLatin1String("light"))
    valid.insert(QStringLiteral("mode"), mode);
  const QString family = appearance.value(QStringLiteral("fontFamily")).toString();
  if (!family.isEmpty() && family.size() <= 512)
    valid.insert(QStringLiteral("fontFamily"), family);
  m_appearance = valid;
  const QString json = QString::fromUtf8(
      QJsonDocument::fromVariant(valid).toJson(QJsonDocument::Compact));
  page()->runJavaScript(QStringLiteral("window.applyAppearance && window.applyAppearance(%1);").arg(json));
}

void EmbeddedSettingsView::refreshSettings() {
  emit m_bridge->changed(m_bridge->snapshot());
}
