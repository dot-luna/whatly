#ifndef EMBEDDEDSETTINGSBRIDGE_H
#define EMBEDDEDSETTINGSBRIDGE_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QVector>
#include <QWidget>

class SettingsWidget;

// Exposed only to the bundled settings document's private WebChannel. The
// WhatsApp page never receives this object or its settings values. Mutations
// address a captured allowlist of real UI controls, using their existing slots.
class EmbeddedSettingsBridge : public QObject {
  Q_OBJECT

public:
  explicit EmbeddedSettingsBridge(SettingsWidget *settings,
                                  QObject *parent = nullptr);

public slots:
  QVariantMap snapshot() const;
  QVariantMap setValue(const QString &id, const QVariant &value);
  QVariantMap activate(const QString &id);
  void close();

signals:
  void changed(const QVariantMap &snapshot);
  void closeRequested();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  struct Control {
    QPointer<QWidget> widget;
    QString label;
    QString group;
    QString rowLabel;
  };
  struct Section {
    QString id;
    QString title;
    QStringList controls;
  };

  void buildControls();
  void observe(QWidget *widget);
  void queueChanged();
  QVariantMap describe(const QString &id, const Control &control) const;
  QPointer<SettingsWidget> m_settings;
  QHash<QString, Control> m_controls;
  QVector<Section> m_sections;
  bool m_updateQueued = false;
};

#endif
