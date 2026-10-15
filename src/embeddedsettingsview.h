#pragma once

#include <QVariantMap>
#include <QWebEngineView>

class EmbeddedSettingsBridge;
class QWebEngineProfile;
class SettingsWidget;

// The WhatsApp page may position or dismiss the panel, but cannot read settings
// or invoke its controls. Those operations live on a separate local page.
class SettingsNavigationHost : public QObject {
  Q_OBJECT
public:
  explicit SettingsNavigationHost(QObject *parent = nullptr) : QObject(parent) {}
public slots:
  void updatePanel(const QVariantMap &state) { emit panelChanged(state); }
signals:
  void panelChanged(const QVariantMap &state);
};

// A child of the account view, aligned to WhatsApp's right-hand content pane.
// Its private, off-the-record browser context only loads bundled resources.
class EmbeddedSettingsView : public QWebEngineView {
  Q_OBJECT
public:
  explicit EmbeddedSettingsView(SettingsWidget *settings, QWidget *parent);
  ~EmbeddedSettingsView() override;
  void setAppearance(const QVariantMap &appearance);
  void refreshSettings();
signals:
  void closeRequested();
private:
  QWebEngineProfile *m_profile = nullptr;
  QWebEnginePage *m_localPage = nullptr;
  EmbeddedSettingsBridge *m_bridge = nullptr;
  QVariantMap m_appearance;
};
